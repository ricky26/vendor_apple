LOCAL_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
COMMON := $(subst iPhone3G,common,$(LOCAL_DIR))

$(call inherit-product, $(COMMON)/common.mk)

PRODUCT_NAME := iPhone3G
PRODUCT_DEVICE := iPhone3G


PRODUCT_COPY_FILES += \
	$(LOCAL_DIR)gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
	$(LOCAL_DIR)init.iphone.rc:root/init.iphone.rc
#	$(LIBERTAS)libertas_sdio.ko:system/lib/modules/libertas_sdio.ko \

