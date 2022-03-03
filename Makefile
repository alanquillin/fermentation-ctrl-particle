GREP = $(shell which grep)
SED = $(shell which sed)
PARTICLE = $(shell which particle)

FIRMWARE_VERSION ?= 2.3.0
PLATFORMS = photon p core c electron e p1 argon boron bsom

SAVE_DIR = ./builds/$(FIRMWARE_VERSION)/$(1)
SAVE_PATH = $(call SAVE_DIR,$(1))/fermentation-controller.bin


define compile
	$(if $(wildcard $(call SAVE_DIR,$(PLATFORM))),,$(shell mkdir -p $(call SAVE_DIR,$(PLATFORM))))
	particle compile $(1) --target $(FIRMWARE_VERSION) --saveTo $(call SAVE_PATH,$(1)) $(EXTRA_ARGS) $(EXTRA_COMPILE_ARGS) .
endef

define extract_connected_device_details
	$(info Looking for a USB connected particle device...)
	$(eval DEVICE_DETAILS := $(shell particle usb list))
	
	$(if $(findstring No devices found,$(DEVICE_DETAILS)),$(error No USB connected device found connected))
	
	$(if $(findstring Memory allocation error,$(DEVICE_DETAILS)),$(error There was a memory allocation error trying to retrieve the device details... try reconnecting the device.))
	
	$(eval $(1) := $(shell echo "$(DEVICE_DETAILS)" | $(GREP) -o '\[.*\]' | $(SED) 's/\[\(.*\)\]/\1/'))
	$(eval PLATFORM_INFO := $(shell echo "$(DEVICE_DETAILS)" | $(GREP) -o '(.*)' | $(SED) 's/(\(.*\))/\1/' | tr A-Z a-z))
	$(eval $(2) := $(shell echo "$(PLATFORM_INFO)" | sed 's/,.*//'))
	$(eval $(3) := $(findstring dfu,$(PLATFORM_INFO)))
endef

define validate_platform
	$(if $(filter $(1),$(PLATFORMS)),,$(error Invalid platform '$(1)'.  Must be one of: $(PLATFORMS)))
endef

.PHONY: config depends compile flash-local flash-cloud clean $(PLATFORMS)

depends:
	particle library copy HttpClient && \
	particle library copy ArduinoJson && \
	particle library copy DS18B20 && \
	particle library copy LiquidCrystal_I2C_Spark

config:
ifneq ($(shell grep "<__IP_Address__>" "src/fermentation-controller.ino"),)
	@echo "IP Address not set.  What is the IP Address of you RPi Light Service? "; \
    read IP_ADDRESS; if [ ! -z $$IP_ADDRESS ]; then sed -i "" "s/<__IP_Address__>/$$IP_ADDRESS/g" src/fermentation-controller.ino; fi
endif

compile: config depends
ifeq ($(PLATFORM),)
	$(call extract_connected_device_details,DEVICE_ID,PLATFORM,DFU)
else
	$(call validate_platform,$(PLATFORM))
endif

	$(call compile,$(PLATFORM))

flash-local: config depends
	$(call extract_connected_device_details,DEVICE_ID,PLATFORM,DFU)

	$(info Connected Device -> $(DEVICE_DETAILS))
	$(info DEVICE_ID -> $(DEVICE_ID))
	$(info PLATFORM -> $(PLATFORM))
	$(info DFU -> $(DFU))

	$(if $(findstring dfu,$(DFU)),,$(error Device not in DFU mode.  See https://docs.particle.io/tutorials/device-os/led/argon/#dfu-mode-device-firmware-upgrade- to learn how to put your device into DFU mode.))

	$(call compile,$(PLATFORM))
	particle flash --target $(FIRMWARE_VERSION) --usb $(EXTRA_ARGS) $(EXTRA_FLASH_ARGS) $(call SAVE_PATH,$(PLATFORM))

flash-cloud: config depends
ifeq ($(DEVICE_ID),)
	$(call extract_connected_device_details,DEVICE_ID,PLATFORM,DFU)
endif
	
	particle flash --target $(FIRMWARE_VERSION) $(EXTRA_ARGS) $(EXTRA_FLASH_ARGS) $(DEVICE_ID) .


clean:
	rm -rf ./builds && \
	rm -rf ./target && \
	rm -rf ./lib && \
	rm -f src/fermentation-controller.cpp && \
	sed -i "" "s/[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}\.[0-9]\{1,3\}/<__IP_Address__>/" src/fermentation-controller.ino

$(PLATFORMS):
    #EMPTY target to catch if the platform was supplies as a command line arg (i.e. `make compile argon`)
	@echo > /dev/null