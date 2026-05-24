obj-m += ouichefs.o
ouichefs-objs := fs.o super.o inode.o file.o dir.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

CURRENT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

all:
	make -C $(KERNELDIR) M=$(CURRENT_DIR) modules

debug:
	make -C $(KERNELDIR) M=$(CURRENT_DIR) ccflags-y+="-DDEBUG -g" modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_DIR) clean
	rm -rf *~
	rm -rf $(depsdir)
	$(MAKE) -C ./mkfs mrproper

.PHONY: all clean
