insmod /share/ouichefs.ko
mkdir -p /mnt/test_mnt
mount /dev/sdc /mnt/test_mnt
rm -f /mnt/test_mnt/*
gcc /share/test_monothread.c -o /tmp/test_mono.out
gcc /share/test_multithread.c -pthread -o /tmp/test_multi.out
gcc /share/run_ioctl.c -o /tmp/ioctl.out

cd /mnt/test_mnt
/tmp/test_mono.out | tee /tmp/test_run_mono.txt
/tmp/test_multi.out | tee /tmp/test_run_multi.txt

cd /
umount /mnt/test_mnt
rmmod ouichefs
