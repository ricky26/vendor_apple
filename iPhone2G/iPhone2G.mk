LOCAL_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
COMMON := $(subst iPhone2G,common,$(LOCAL_DIR))

$(call inherit-product, $(COMMON)/common.mk)

PRODUCT_NAME := iPhone2G
PRODUCT_DEVICE := iPhone2G

PRODUCT_COPY_FILES += \
	$(LOCAL_DIR)gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
	$(LOCAL_DIR)asound.conf:system/etc/asound.conf \
	$(LOCAL_DIR)asound.state:system/etc/asound.state \
	$(LOCAL_DIR)init.rc:root/init.$(TARGET_BOARD_PLATFORM).rc

