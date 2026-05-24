nproc=$(shell nproc)

kdir=./linux-6.5.7
kconfig=$(kdir)/.config

ouichefsdir=./ouichefs
test_disk=./test.img
mkfs=./ouichefs/mkfs/mkfs.ouichefs

home_disk=./myHome.img
boot_disk=./pnl-tp-2021.img
share=./share

all: ouichefs $(boot_disk) $(home_disk) $(share) $(test_disk)

ouichefs: kernel $(test_disk)
	$(MAKE) -C $(ouichefsdir) KERNELDIR=../$(kdir)

kernel: $(kconfig)
	time $(MAKE) -C $(kdir) -j$(nproc)

$(kconfig):
	cp ./config-lkp $(kconfig)

clean:
	$(MAKE) -C $(kdir) clean
	rm -f $(test_disk) $(myHome) $(boot_disk).xz

$(test_disk): $(mkfs)
	dd if=/dev/zero of=$(test_disk) bs=1M count=50
	$(mkfs) $(test_disk)

$(home_disk):
	dd if=/dev/zero of=$(home_disk) bs=1M count=50
	mkfs.ext4 $(home_disk)
	# Fix for earlier versions of ext4
	tune2fs -O ^orphan_file $(home_disk)

$(share):
	mkdir -p $(share)

$(mkfs):
	$(MAKE) -C $(ouichefs)/mkfs

$(boot_disk):
	cp boot.img.xz $(boot_disk).xz
	xz -d $(boot_disk).xz

.PHONY: ouichefs kernel clean all
