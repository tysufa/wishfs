## Makefile — documentation
## Usage:
##   make            # builds kernel, ouichefs, disks and share
##   make kernel     # builds the Linux kernel in the bundled tree
##   make ouichefs   # builds the ouichefs userspace module (depends on kernel)
##   make clean      # clean kernel build and remove generated images
##
## Important variables (can be overridden on the command line):
##   nproc       - number of parallel jobs (defaults to output of `nproc`)
##   kdir        - path to the Linux kernel source tree
##   ouichefsdir - path to the ouichefs userspace tree
##   test_disk   - filename for generated test filesystem image
##   mkfs        - path to the ouichefs mkfs binary used to format $(test_disk)
##
## Typical examples:
##   make                # build everything using defaults
##   make nproc=8 kernel # build kernel with 8 parallel jobs
##
nproc=$(shell nproc)

kdir=./linux-6.5.7
kconfig=$(kdir)/.config

ouichefsdir=./ouichefs
test_disk=./test.img
test_disk2=./test2.img
mkfs=./ouichefs/mkfs/mkfs.ouichefs

home_disk=./myHome.img
boot_disk=./pnl-tp-2021.img
share=./share

all: ouichefs $(boot_disk) $(home_disk) $(share) $(test_disk) $(test_disk2)

ouichefs: kernel $(test_disk)
	$(MAKE) -C $(ouichefsdir) KERNELDIR=../$(kdir)
	cp $(ouichefsdir)/ouichefs.ko $(share)
	cp tests/* $(share)

kernel: $(kdir) $(kconfig)
	time $(MAKE) -C $(kdir) -j$(nproc)

$(kconfig):
	cp ./config-lkp $(kconfig)

clean_disk:
	rm -f $(test_disk) $(test_disk2)

clean:
	$(MAKE) -C $(kdir) clean
	rm -f $(test_disk) $(test_disk2) $(myHome)
	rm -f $(kconfig)

$(test_disk): $(mkfs)
	dd if=/dev/zero of=$(test_disk) bs=1M count=50
	$(mkfs) $(test_disk)

$(test_disk2): $(mkfs)
	dd if=/dev/zero of=$(test_disk2) bs=1M count=42
	$(mkfs) $(test_disk2)

$(home_disk):
	dd if=/dev/zero of=$(home_disk) bs=1M count=50
	mkfs.ext4 $(home_disk)
	# Fix for earlier versions of ext4
	tune2fs -O ^orphan_file $(home_disk)

$(share):
	mkdir -p $(share)

$(mkfs):
	$(MAKE) -C $(ouichefsdir)/mkfs

$(boot_disk):
	wget https://etyloppihacilem.com/collection/pnl-tp-2021.img.xz
	xz -d pnl-tp-2021.img.xz

$(kdir):
	wget https://etyloppihacilem.com/collection/linux-6.5.7.tar.xz
	tar -xvf linux-6.5.7.tar.xz
	rm linux-6.5.7.tar.xz

compile_commands: clean
	bear -- $(MAKE)

.PHONY: ouichefs kernel clean all compile_commands
