kconfig=./linux-6.5.7/.config
nproc=$(shell nproc)

all:
	$(MAKE) kernel

kernel: $(kconfig)
	$(MAKE) -C ./linux-6.5.7 -j$(nproc)

$(kconfig):
	cp ./config-lkp ./linux-6.5.7/.config
