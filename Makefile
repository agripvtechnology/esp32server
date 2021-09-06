#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := agripv

include $(IDF_PATH)/make/project.mk

ifdef CONFIG_WEB_DEPLOY_SF
WEB_SRC_DIR = $(shell pwd)/front/web-demo
ifneq ($(wildcard $(WEB_SRC_DIR)/dist/.*),)
SPIFFS_IMAGE_FLASH_IN_PROJECT := 1
$(eval $(call spiffs_create_partition_image,www,$(WEB_SRC_DIR)/dist))
else
$(error $(WEB_SRC_DIR)/dist doesn't exist. Please run 'npm run build' in $(WEB_SRC_DIR))
endif
endif