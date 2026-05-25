insmod /share/ouichefs.ko
mkdir -p /mnt/test_mnt
mount /dev/sdc /mnt/test_mnt
rm /mnt/test_mnt/*
gcc /share/test.c -o /tmp/test.out

/tmp/test.out | tee /tmp/test_run.txt

umount /mnt/test_mnt
rmmod ouichefs
