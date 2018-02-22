dma_proxy-objs := dma_proxy_driver.o axi_dma_iface.o
obj-m += dma_proxy.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean