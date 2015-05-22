LOCAL_PATH := $(call my-dir)
PERL = perl

## Don't change anything under here. The variables are named M8_whatever
## on purpose, to avoid conflicts with similarly named variables at other
## parts of the build environment

## Imported from the original makefile...
KERNEL_CONFIG := $(KERNEL_OUT)/.config

M8_DTS_FILES = $(wildcard $(TOP)/$(TARGET_KERNEL_SOURCE)/arch/arm/boot/dts/msm8974pro-ab-pm8941-m8*.dts)
M8_DTS_FILE = $(lastword $(subst /, ,$(1)))
DTB_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%.dtb,$(call M8_DTS_FILE,$(1))))
ZIMG_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%-zImage,$(call M8_DTS_FILE,$(1))))
KERNEL_ZIMG = $(KERNEL_OUT)/arch/arm/boot/zImage
DTC = $(KERNEL_OUT)/scripts/dtc/dtc
DTBTAGNAME := "htc,project-id = <"

define append-m8-dtb
   mkdir -p $(KERNEL_OUT)/arch/arm/boot;\
   $(foreach d, $(M8_DTS_FILES), \
      cat $(KERNEL_ZIMG) $(call DTB_FILE,$(d)) > $(call ZIMG_FILE,$(d));)
endef

## Build and run dtbtool
DTBTOOL := $(HOST_OUT_EXECUTABLES)/dtbToolCM$(HOST_EXECUTABLE_SUFFIX)
INSTALLED_DTIMAGE_TARGET := $(PRODUCT_OUT)/dt.img

$(INSTALLED_DTIMAGE_TARGET): $(DTBTOOL) $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr $(INSTALLED_KERNEL_TARGET)
	@echo -e ${CL_CYN}"Start DT image: $@"${CL_RST}
	$(call append-m8-dtb)
	$(call pretty,"Target dt image: $(INSTALLED_DTIMAGE_TARGET)")
	$(hide) $(DTBTOOL) -o $(INSTALLED_DTIMAGE_TARGET) -s $(BOARD_KERNEL_PAGESIZE) -d $(DTBTAGNAME) -p $(KERNEL_OUT)/scripts/dtc/ $(KERNEL_OUT)/arch/arm/boot/
	@echo -e ${CL_CYN}"Made DT image: $@"${CL_RST}


## Overload bootimg generation: Same as the original, + --dt arg
$(INSTALLED_BOOTIMAGE_TARGET): $(MKBOOTIMG) $(INTERNAL_BOOTIMAGE_FILES) $(INSTALLED_DTIMAGE_TARGET)
	$(call pretty,"Target boot image: $@")
	$(hide) $(MKBOOTIMG) $(INTERNAL_BOOTIMAGE_ARGS) $(BOARD_MKBOOTIMG_ARGS) --dt $(INSTALLED_DTIMAGE_TARGET) --output $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE),raw)
	@echo -e ${CL_CYN}"Made boot image: $@"${CL_RST}

## Overload recoveryimg generation: Same as the original, + --dt arg
$(INSTALLED_RECOVERYIMAGE_TARGET): $(MKBOOTIMG) $(INSTALLED_DTIMAGE_TARGET) \
		$(recovery_ramdisk) \
		$(recovery_kernel)
	@echo -e ${CL_CYN}"----- Making recovery image ------"${CL_RST}
	$(hide) $(MKBOOTIMG) $(INTERNAL_RECOVERYIMAGE_ARGS) $(BOARD_MKBOOTIMG_ARGS) --dt $(INSTALLED_DTIMAGE_TARGET) --output $@
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE),raw)
	@echo -e ${CL_CYN}"Made recovery image: $@"${CL_RST}
