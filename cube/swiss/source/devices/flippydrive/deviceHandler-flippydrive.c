/* deviceHandler-flippydrive.c
	- device implementation for FlippyDrive
	by Extrems
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include "deviceHandler.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "flippy.h"

file_handle initial_Flippy =
	{ "fldrv:/",      // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};

file_handle initial_FlippyFlash =
	{ "flffs:/",      // directory
	  0ULL,     // fileBase (u64)
	  0,        // offset
	  0,        // size
	  IS_DIR,
	  0,
	  0
	};

device_info initial_Flippy_info = {
	0LL,
	0LL,
	true
};

device_info* deviceHandler_Flippy_info(file_handle* file) {
	return &initial_Flippy_info;
}

s32 deviceHandler_Flippy_readDir(file_handle* ffile, file_handle** dir, u32 type) {

	flippydirinfo* dp = memalign(32, sizeof(flippydirinfo));
	if((getDeviceFromPath(ffile->name) == &__device_flippyflash ?
		flippy_flash_opendir(dp, getDevicePath(ffile->name)) :
		flippy_opendir(dp, getDevicePath(ffile->name))) != FLIPPY_RESULT_OK) return -1;
	flippyfilestat entry;
	
	// Set everything up to read
	int num_entries = 1, i = 1;
	*dir = calloc(num_entries, sizeof(file_handle));
	concat_path((*dir)[0].name, ffile->name, "..");
	(*dir)[0].fileAttrib = IS_SPECIAL;
	
	// Read each entry of the directory
	while( flippy_readdir(dp, &entry) == FLIPPY_RESULT_OK && entry.name[0] != '\0') {
		if(!strcmp(entry.name, ".") || !strcmp(entry.name, "..")) {
			continue;
		}
		// Do we want this one?
		if((type == -1 || ((entry.type == FLIPPY_TYPE_DIR) ? (type==IS_DIR) : (type==IS_FILE)))) {
			if(entry.type == FLIPPY_TYPE_FILE) {
				if(!checkExtension(entry.name)) continue;
			}
			// Make sure we have room for this one
			if(i == num_entries){
				++num_entries;
				*dir = reallocarray(*dir, num_entries, sizeof(file_handle));
			}
			memset(&(*dir)[i], 0, sizeof(file_handle));
			if(concat_path((*dir)[i].name, ffile->name, entry.name) < PATHNAME_MAX && entry.size <= UINT32_MAX) {
				(*dir)[i].size       = entry.size;
				(*dir)[i].fileAttrib = (entry.type == FLIPPY_TYPE_DIR) ? IS_DIR : IS_FILE;
				++i;
			}
		}
	}
	
	flippy_closedir(dp);
	free(dp);
	return i;
}

s64 deviceHandler_Flippy_seekFile(file_handle* file, s64 where, u32 type) {
	if(type == DEVICE_HANDLER_SEEK_SET) file->offset = where;
	else if(type == DEVICE_HANDLER_SEEK_CUR) file->offset = file->offset + where;
	else if(type == DEVICE_HANDLER_SEEK_END) file->offset = file->size + where;
	return file->offset;
}

s32 deviceHandler_Flippy_readFile(file_handle* file, void* buffer, u32 length) {
	if(file->status == STATUS_MAPPED) {
		s32 bytes_read = DVD_ReadAbs((dvdcmdblk*)file->other, buffer, length, file->offset);
		if(bytes_read > 0) file->offset += bytes_read;
		return bytes_read;
	}
	if(!file->fp) {
		file->fp = memalign(32, sizeof(flippyfileinfo));
		if((getDeviceFromPath(file->name) == &__device_flippyflash ?
			flippy_flash_open(file->fp, getDevicePath(file->name), FLIPPY_FLAG_DEFAULT) :
			flippy_open(file->fp, getDevicePath(file->name), FLIPPY_FLAG_DEFAULT)) != FLIPPY_RESULT_OK) {
			free(file->fp);
			file->fp = NULL;
			return -1;
		}
	}
	flippyfileinfo* info = file->fp;
	if(file->offset > info->file.size) {
		file->offset = info->file.size;
	}
	if(length + file->offset > info->file.size) {
		length = info->file.size - file->offset;
	}
	if(flippy_pread(file->fp, buffer, length, file->offset) != FLIPPY_RESULT_OK) {
		return -1;
	}
	file->offset += length;
	file->size = info->file.size;
	return length;
}

s32 deviceHandler_Flippy_writeFile(file_handle* file, const void* buffer, u32 length) {
	if(!file->fp) {
		file->fp = memalign(32, sizeof(flippyfileinfo));
		if((getDeviceFromPath(file->name) == &__device_flippyflash ?
			flippy_flash_open(file->fp, getDevicePath(file->name), FLIPPY_FLAG_DEFAULT | FLIPPY_FLAG_WRITE) :
			flippy_open(file->fp, getDevicePath(file->name), FLIPPY_FLAG_DEFAULT | FLIPPY_FLAG_WRITE)) != FLIPPY_RESULT_OK) {
			free(file->fp);
			file->fp = NULL;
			return -1;
		}
	}
	flippyfileinfo* info = file->fp;
	if(flippy_pwrite(file->fp, buffer, length, file->offset) != FLIPPY_RESULT_OK) {
		return -1;
	}
	file->offset += length;
	file->size = info->file.size;
	return length;
}

s32 deviceHandler_Flippy_setupFile(file_handle* file, file_handle* file2, ExecutableFile* filesToPatch, int numToPatch) {
	file_frag *fragList = NULL;
	u32 numFrags = 0;
	
	if(numToPatch < 0) {
		if(!getFragments(DEVICE_CUR, file, &fragList, &numFrags, 0, 0, 0) || numFrags != 1) {
			free(fragList);
			return 0;
		}
		if(flippy_mount(file->fp) != FLIPPY_RESULT_OK) {
			free(fragList);
			return 0;
		}
		free(fragList);
		file->status = STATUS_MAPPED;
		return 1;
	}
	return 0;
}

s32 deviceHandler_Flippy_init(file_handle* file) {
	return flippy_init() == FLIPPY_RESULT_OK ? 0 : ENODEV;
}

s32 deviceHandler_Flippy_closeFile(file_handle* file) {
	int ret = 0;
	if(file && file->fp) {
		ret = flippy_close(file->fp);
		free(file->fp);
		file->fp = NULL;
		file->status = STATUS_NOT_MAPPED;
	}
	return ret;
}

s32 deviceHandler_Flippy_deinit(file_handle* file) {
	deviceHandler_Flippy_closeFile(file);
	return 0;
}

s32 deviceHandler_Flippy_deleteFile(file_handle* file) {
	deviceHandler_Flippy_closeFile(file);
	if(getDeviceFromPath(file->name) == &__device_flippyflash)
		return flippy_flash_unlink(getDevicePath(file->name));
	else
		return flippy_unlink(getDevicePath(file->name));
}

s32 deviceHandler_Flippy_renameFile(file_handle* file, char* name) {
	deviceHandler_Flippy_closeFile(file);
	int ret = flippy_rename(getDevicePath(file->name), getDevicePath(name));
	strcpy(file->name, name);
	return ret;
}

s32 deviceHandler_Flippy_makeDir(file_handle* dir) {
	return flippy_mkdir(getDevicePath(dir->name));
}

bool deviceHandler_Flippy_test() {
	while (DVD_LowGetCoverStatus() == 0);
	
	if (swissSettings.hasDVDDrive) {
		switch (driveInfo.rel_date) {
			case 0x20220420:
				if (flippy_boot(FLIPPY_MODE_BOOT) != FLIPPY_RESULT_OK)
					return false;
				if (flippy_boot(FLIPPY_MODE_NOUPDATE) != FLIPPY_RESULT_OK)
					return false;
				
				while (driveInfo.rel_date != 0x20220426)
					if (DVD_Inquiry(&commandBlock, &driveInfo) < 0)
						return false;
			case 0x20220426:
				return true;
		}
	}
	
	return false;
}

bool deviceHandler_FlippyFlash_test() {
	return deviceHandler_getDeviceAvailable(&__device_flippy);
}

char* deviceHandler_Flippy_status(file_handle* file) {
	return NULL;
}

DEVICEHANDLER_INTERFACE __device_flippy = {
	.deviceUniqueId = DEVICE_ID_J,
	.hwName = "FlippyDrive",
	.deviceName = "FlippyDrive",
	.deviceDescription = "Supported File System(s): FAT16, FAT32, exFAT",
	.deviceTexture = {TEX_FLIPPY, 102, 56, 104, 58},
	.features = FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_CONFIG_DEVICE|FEAT_THREAD_SAFE|FEAT_AUDIO_STREAMING,
	.location = LOC_DVD_CONNECTOR,
	.initial = &initial_Flippy,
	.test = deviceHandler_Flippy_test,
	.info = deviceHandler_Flippy_info,
	.init = deviceHandler_Flippy_init,
	.makeDir = deviceHandler_Flippy_makeDir,
	.readDir = deviceHandler_Flippy_readDir,
	.seekFile = deviceHandler_Flippy_seekFile,
	.readFile = deviceHandler_Flippy_readFile,
	.writeFile = deviceHandler_Flippy_writeFile,
	.closeFile = deviceHandler_Flippy_closeFile,
	.deleteFile = deviceHandler_Flippy_deleteFile,
	.renameFile = deviceHandler_Flippy_renameFile,
	.setupFile = deviceHandler_Flippy_setupFile,
	.deinit = deviceHandler_Flippy_deinit,
	.status = deviceHandler_Flippy_status,
};

DEVICEHANDLER_INTERFACE __device_flippyflash = {
	.deviceUniqueId = DEVICE_ID_K,
	.hwName = "FlippyDrive",
	.deviceName = "FlippyDrive Flash",
	.deviceDescription = "Supported File System(s): FAT12",
	.deviceTexture = {TEX_FLIPPY, 102, 56, 104, 58},
	.features = FEAT_READ|FEAT_WRITE|FEAT_BOOT_GCM|FEAT_BOOT_DEVICE|FEAT_AUDIO_STREAMING,
	.location = LOC_SYSTEM,
	.initial = &initial_FlippyFlash,
	.test = deviceHandler_FlippyFlash_test,
	.info = deviceHandler_Flippy_info,
	.init = deviceHandler_Flippy_init,
	.readDir = deviceHandler_Flippy_readDir,
	.seekFile = deviceHandler_Flippy_seekFile,
	.readFile = deviceHandler_Flippy_readFile,
	.writeFile = deviceHandler_Flippy_writeFile,
	.closeFile = deviceHandler_Flippy_closeFile,
	.deleteFile = deviceHandler_Flippy_deleteFile,
	.setupFile = deviceHandler_Flippy_setupFile,
	.deinit = deviceHandler_Flippy_deinit,
	.status = deviceHandler_Flippy_status,
};
