nproc=$(shell nproc)

kdir=./linux-6.5.7
kconfig=$(kdir)/.config

ouichefsdir=./ouichefs
test_disk=./test.img
mkfs=./ouichefs/mkfs/mkfs.ouichefs

ouichefs: $(test_disk)
	$(MAKE) -C $(ouichefsdir) KERNELDIR=../$(kdir)

kernel: $(kconfig)
	$(MAKE) -C $(kdir) -j$(nproc)

$(kconfig):
	cp ./config-lkp $(kconfig)

clean:
	$(MAKE) -C $(kdir) clean
	rm $(test_disk)

$(test_disk): $(mkfs)
	dd if=/dev/zero of=$(test_disk) bs=1M count=50
	$(mkfs) $(test_disk)

$(mkfs):
	$(MAKE) -C $(ouichefs)/mkfs

.PHONY: ouichefs kernel clean
