insmod /share/ouichefs.ko
mkdir -p /mnt/test_mnt
mount /dev/sdc /mnt/test_mnt
rm /mnt/test_mnt/*
gcc /share/test_monothread.c -o /tmp/test_mono.out
gcc /share/test_multithread.c -o /tmp/test_multi.out

/tmp/test_mono.out | tee /tmp/test_run_mono.txt
/tmp/test_multi.out | tee /tmp/test_run_multi.txt

umount /mnt/test_mnt
rmmod ouichefs
