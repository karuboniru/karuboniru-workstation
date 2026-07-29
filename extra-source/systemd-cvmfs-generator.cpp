#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr std::string_view kDefaultConfigPath =
    "/etc/systemd-cvmfs-generator/repositories.conf";
constexpr std::string_view kMountRoot = "/cvmfs";

void log_error(const std::string &message) {
  std::cerr << "systemd-cvmfs-generator: " << message << '\n';

  if (std::getenv("SYSTEMD_CVMFS_GENERATOR_NO_KMSG") != nullptr) {
    return;
  }

  const int fd = ::open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) {
    return;
  }

  const std::string record = "<3>systemd-cvmfs-generator: " + message + "\n";
  static_cast<void>(::write(fd, record.data(), record.size()));
  static_cast<void>(::close(fd));
}

std::string trim(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }

  std::size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }

  return std::string(value.substr(first, last - first));
}

bool valid_repository_name(std::string_view repository) {
  if (repository.empty() || repository.size() > 253 ||
      repository.front() == '.' || repository.back() == '.' ||
      repository.find("..") != std::string_view::npos) {
    return false;
  }

  std::size_t component_length = 0;
  for (const char character : repository) {
    if (character == '.') {
      if (component_length == 0 || component_length > 63) {
        return false;
      }
      component_length = 0;
      continue;
    }

    const auto value = static_cast<unsigned char>(character);
    if (std::isalnum(value) == 0 && character != '_' && character != '-') {
      return false;
    }
    ++component_length;
  }

  return component_length > 0 && component_length <= 63;
}

std::vector<std::string> read_repositories(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      throw std::runtime_error("cannot inspect " + path.string() + ": " +
                               error.message());
    }
    return {};
  }

  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open " + path.string());
  }

  std::set<std::string> seen;
  std::vector<std::string> repositories;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (const auto comment = line.find('#'); comment != std::string::npos) {
      line.erase(comment);
    }

    std::string repository = trim(line);
    if (repository.empty()) {
      continue;
    }
    if (!valid_repository_name(repository)) {
      throw std::runtime_error(
          path.string() + ':' + std::to_string(line_number) +
          ": invalid repository name '" + repository + "'");
    }
    if (seen.insert(repository).second) {
      repositories.push_back(std::move(repository));
    }
  }

  if (!input.eof()) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return repositories;
}

std::string unit_name_for_path(std::string_view path) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(path.size());

  bool at_start = true;
  for (const char raw_character : path) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (character == '/') {
      if (!at_start && !result.empty() && result.back() != '-') {
        result.push_back('-');
      }
      at_start = false;
      continue;
    }
    at_start = false;

    if (std::isalnum(character) != 0 || character == ':' || character == '_' ||
        character == '.') {
      result.push_back(static_cast<char>(character));
      continue;
    }

    result.append("\\x");
    result.push_back(hex[character >> 4U]);
    result.push_back(hex[character & 0x0fU]);
  }

  while (!result.empty() && result.back() == '-') {
    result.pop_back();
  }
  return result.empty() ? "-" : result;
}

enum class CreateResult { created, already_exists };

CreateResult write_file_exclusive(const std::filesystem::path &path,
                                  std::string_view content) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd < 0) {
    if (errno == EEXIST) {
      return CreateResult::already_exists;
    }
    throw std::runtime_error("cannot create " + path.string() + ": " +
                             std::strerror(errno));
  }

  std::size_t offset = 0;
  while (offset < content.size()) {
    const ssize_t written =
        ::write(fd, content.data() + offset, content.size() - offset);
    if (written < 0) {
      const int write_error = errno;
      static_cast<void>(::close(fd));
      static_cast<void>(::unlink(path.c_str()));
      throw std::runtime_error("cannot write " + path.string() + ": " +
                               std::strerror(write_error));
    }
    offset += static_cast<std::size_t>(written);
  }

  if (::close(fd) < 0) {
    const int close_error = errno;
    static_cast<void>(::unlink(path.c_str()));
    throw std::runtime_error("cannot close " + path.string() + ": " +
                             std::strerror(close_error));
  }
  return CreateResult::created;
}

void create_relative_symlink(const std::filesystem::path &path,
                             const std::filesystem::path &target) {
  if (::symlink(target.c_str(), path.c_str()) == 0) {
    return;
  }
  if (errno != EEXIST) {
    throw std::runtime_error("cannot create symlink " + path.string() + ": " +
                             std::strerror(errno));
  }

  std::error_code error;
  const std::filesystem::path existing =
      std::filesystem::read_symlink(path, error);
  if (error || existing != target) {
    throw std::runtime_error("refusing to replace existing path " +
                             path.string());
  }
}

std::string mount_unit(std::string_view repository,
                       const std::filesystem::path &source_path) {
  return "# Automatically generated by systemd-cvmfs-generator\n\n"
         "[Unit]\n"
         "Documentation=man:systemd.mount(5) man:systemd.automount(5)\n"
         "SourcePath=" +
         source_path.string() +
         "\n\n"
         "[Mount]\n"
         "What=" +
         std::string(repository) +
         "\n"
         "Where=/cvmfs/" +
         std::string(repository) +
         "\n"
         "Type=cvmfs\n"
         "Options=auto,nofail,_netdev,x-systemd.automount,X-mount.mkdir\n";
}

std::string automount_unit(std::string_view repository,
                           const std::filesystem::path &source_path) {
  return "# Automatically generated by systemd-cvmfs-generator\n\n"
         "[Unit]\n"
         "Documentation=man:systemd.mount(5) man:systemd.automount(5)\n"
         "SourcePath=" +
         source_path.string() +
         "\n\n"
         "[Automount]\n"
         "Where=/cvmfs/" +
         std::string(repository) + "\n";
}

void generate(const std::filesystem::path &output_directory,
              const std::filesystem::path &config_path,
              const std::vector<std::string> &repositories) {
  if (repositories.empty()) {
    return;
  }

  const std::filesystem::path wants_directory =
      output_directory / "remote-fs.target.wants";
  std::error_code error;
  std::filesystem::create_directories(wants_directory, error);
  if (error) {
    throw std::runtime_error("cannot create " + wants_directory.string() +
                             ": " + error.message());
  }

  for (const std::string &repository : repositories) {
    const std::string mount_path = std::string(kMountRoot) + '/' + repository;
    const std::string base_name = unit_name_for_path(mount_path);
    const std::string mount_name = base_name + ".mount";
    const std::string automount_name = base_name + ".automount";

    static_cast<void>(write_file_exclusive(
        output_directory / mount_name, mount_unit(repository, config_path)));
    static_cast<void>(
        write_file_exclusive(output_directory / automount_name,
                             automount_unit(repository, config_path)));
    create_relative_symlink(wants_directory / automount_name,
                            std::filesystem::path("..") / automount_name);
  }
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc != 2 && argc != 4) {
    log_error("expected normal-dir [early-dir late-dir]");
    return EXIT_FAILURE;
  }

  if (const char *initrd = std::getenv("SYSTEMD_IN_INITRD");
      initrd != nullptr && std::string_view(initrd) == "1") {
    return EXIT_SUCCESS;
  }

  const char *configured_path = std::getenv("SYSTEMD_CVMFS_GENERATOR_CONFIG");
  const std::filesystem::path config_path =
      configured_path == nullptr ? std::filesystem::path(kDefaultConfigPath)
                                 : std::filesystem::path(configured_path);

  try {
    const std::vector<std::string> repositories =
        read_repositories(config_path);
    generate(argv[1], config_path, repositories);
  } catch (const std::exception &exception) {
    log_error(exception.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
