#!/usr/bin/bash
set -eu
printf 'Outer AppArmor profile: '
cat /proc/self/attr/current || true
rpm -q systemd bubblewrap
probe=$(mktemp -d /tmp/apparmor-probe.XXXXXX)
trap 'chmod -R u+rwX "$probe"; rm -rf "$probe"' EXIT
mkdir -p "$probe/etc" "$probe/usr/lib/sysusers.d"
printf 'root:x:0:0:root:/root:/bin/sh\n' > "$probe/etc/passwd"
printf 'root:x:0:\n' > "$probe/etc/group"
printf 'root:!::\n' > "$probe/etc/gshadow"
printf 'root:!*:1:0:99999:7:::\n' > "$probe/etc/shadow"
chmod 000 "$probe/etc/gshadow" "$probe/etc/shadow"
printf 'u diagnostic - "Diagnostic" / /usr/sbin/nologin\n' > "$probe/usr/lib/sysusers.d/diagnostic.conf"
export probe
set +e
bwrap --bind / / --proc /proc --dev /dev --cap-add ALL /bin/sh -c '
  printf "Inner AppArmor profile: "
  cat /proc/self/attr/current
  grep "^Cap" /proc/self/status
  cat "$probe/etc/gshadow"
'
printf 'bwrap mode-000 read exit: %s\n' "$?"
bwrap --bind / / --proc /proc --dev /dev --cap-add ALL /usr/bin/systemd-sysusers --root="$probe"
printf 'bwrap sysusers exit: %s\n' "$?"
