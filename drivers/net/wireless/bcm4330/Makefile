# bcm4330 & bcm43362
DHDCFLAGS = -fshort-wchar -DLINUX -DBCMDRIVER -DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DWLBTAMP -DBCMFILEIMAGE -DDHDTHREAD -DDHD_GPL -DDHD_SCHED -DBDC -DTOE -DDHD_BCMEVENTS -DSHOW_EVENTS -DDONGLEOVERLAYS -DBCMSDIO -DDHD_GPL -DBCMLXSDMMC -DBCMPLATFORM_BUS -DWIFI_ACT_FRAME -DMMC_SDIO_ABORT -DEMBEDDED_PLATFORM -DARP_OFFLOAD_SUPPORT -DPKT_FILTER_SUPPORT -DSET_RANDOM_MAC_SOFTAP -DKEEP_ALIVE -DPNO_SUPPORT -DDHD_DEBUG -DSDTEST -Wall -Wstrict-prototypes -Idrivers/net/wireless/bcm4330 -Idrivers/net/wireless/bcm4330/include #-Werror

#DHDCFLAGS += -DSDIO_ISR_THREAD -DCONFIG_WIFI_CONTROL_FUNC -DSOFTAP
DHDCFLAGS += -DCONFIG_WIFI_CONTROL_FUNC -DSOFTAP
DHDCFLAGS += -DBCMWAPI_WPI -DBCMWAPI_WAI
DHDCFLAGS += -DCUSTOMER_HW2 -DCUSTOM_OOB_GPIO_NUM=53 -DOOB_INTR_ONLY -DHW_OOB

DHDOFILES = dhd_linux.o linux_osl.o bcmutils.o dhd_common.o dhd_custom_gpio.o \
	wl_iw.o wl_android.o  wldev_common.o siutils.o sbutils.o aiutils.o hndpmu.o bcmwifi.o dhd_sdio.o   \
	dhd_linux_sched.o dhd_cdc.o bcmsdh_sdmmc.o bcmsdh.o bcmsdh_linux.o    \
	bcmsdh_sdmmc_linux.o bcmevent.o dhd_bta.o

obj-m += bcm4330.o
bcm4330-objs += $(DHDOFILES)
EXTRA_CFLAGS = $(DHDCFLAGS)
EXTRA_LDFLAGS += --strip-debug
