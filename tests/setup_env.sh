insmod /share/ouichefs.ko
mkdir -p /mnt/test_mnt
mount /dev/sdc /mnt/test_mnt
gcc /share/run_ioctl.c -o /tmp/ioctl.out
printf 'A%.0s' {1..2048} > /tmp/a
