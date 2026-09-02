
/* General USB I/O support, HarmonyOS (OHOS) native implementation */
/* using the OHOS USB DDK (libusb_ndk.z.so). */

/* This file is conditionaly #included into usbio.c under __OHOS__ */

/* 
 * Argyll Color Management System
 *
 * Author: Graeme W. Gill
 * Date:   2006/22/4
 *
 * Copyright 2006 - 2013 Graeme W. Gill
 * All rights reserved.
 *
 * This material is licenced under the GNU GENERAL PUBLIC LICENSE Version 2 or later :-
 * see the License2.txt file for licencing details.
 */

/* OHOS changes (port of the i1Pro3 driver subset to HarmonyOS): */
/* - This is a new backend implementing the usbio.h platform interface */
/*   on top of the OHOS USB DDK (usb/usb_ddk_api.h). */
/* - The Linux usbio_lx.c uses /dev/bus/usb file descriptors, URB's and a */
/*   reaper thread; the OHOS DDK only offers synchronous transfers, so all */
/*   transfers are done in-line and no reaper thread is needed. */
/* - Enumeration is done with OH_Usb_GetDevices() + OH_Usb_GetDeviceDescriptor() */
/*   + OH_Usb_GetConfigDescriptor() rather than scanning /dev. */
/* - Opening a port claims the device interfaces with OH_Usb_ClaimInterface(). */
/* - Control transfers use OH_Usb_SendControlReadRequest()/OH_Usb_SendControlWriteRequest(), */
/*   bulk/interrupt transfers use OH_Usb_CreateDeviceMemMap() + OH_Usb_SendPipeRequest(). */
/* - icoms_usb_resetep()/icoms_usb_clearhalt() are no-ops: the DDK has no */
/*   equivalent. Endpoint stall recovery is done at the session level */
/*   instead: on an I/O class transfer error (USB_DDK_FAILED/IO_FAILED/...) */
/*   the driver releases and re-claims all interfaces, and if that fails it */
/*   restarts the whole DDK session (OH_Usb_Release()/OH_Usb_Init() + */
/*   re-enumerate). See ohos_usb_recover() below. */
/* - Bulk/interrupt transfers are paced with a short sleep before each */
/*   OH_Usb_SendPipeRequest(): the DDK HDI has been observed to wedge (a */
/*   transfer blocking for its entire timeout, then the endpoint */
/*   permanently stalling) when transfers arrive in a tight burst. */
/* - OH_Usb_Init() is called on every enumeration (it is idempotent): the */
/*   DDK session may have been released in between (i1probe calls */
/*   OH_Usb_Release()), so a static "already inited" flag is unsafe. */
/* - icoms_usb_transaction() captures UsbDeviceMemMap.transferedLength */
/*   before OH_Usb_DestroyDeviceMemMap(); dereferencing it after destroy */
/*   (as an earlier revision did) crashes on the first successful transfer. */
/* - Key failure branches log to hilog (OH_LOG_ERROR, LOG_APP) because the */
/*   driver process has no console and a1log output is invisible there. */

#include <usb/usb_ddk_api.h>	/* OHOS USB DDK */
#include <hilog/log.h>			/* OHOS hilog - diagnostic logging (LOG_APP) */

/* Max. number of endpoints in a transfer (matches icoms ep[32]) */
#define USBIO_OHOS_MAX_EP	32

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Return the claimed interface handle for the given endpoint address, */
/* or 0 if none claimed. Endpoints can be spread over several interfaces, */
/* so look up the endpoint's interface number in the ep table. */
static unsigned long long ohos_ep_handle(icoms *p, unsigned char endpoint) {
	struct usb_idevice *usbd = p->usbd;
	int ifaceno;

	if (usbd == NULL)
		return 0;

	if (p->EPINFO(endpoint).valid == 0) {
		/* Fall back to interface 0 if the ep table is incomplete */
		ifaceno = 0;
	} else {
		ifaceno = p->EPINFO(endpoint).ifaceno;
	}
	if (ifaceno < 0 || ifaceno >= USBIO_OHOS_MAX_EP)
		return 0;
	return usbd->ifaceHandle[ifaceno];
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Session level stall/error recovery. */
/* */
/* The OHOS USB DDK has no clearhalt/resetep API, and on device a bulk */
/* endpoint has been observed to enter a permanent stall: after one */
/* transfer fails with USB_DDK_IO_FAILED every subsequent */
/* OH_Usb_SendPipeRequest() fails the same way, forever. The Linux and */
/* Windows backends recover from this with icoms_usb_clearhalt(); here we */
/* recover by tearing down and re-establishing the session instead: */
/*   level 1: release all claimed interfaces, pause, re-claim them */
/*            (re-creating the pipes in the HDI driver). */
/*   level 2: OH_Usb_Release() + OH_Usb_Init() + OH_Usb_GetDevices() to */
/*            re-find the device by VID/PID, then re-claim. */
/* Recovery is triggered from the transfer failure paths below; the failed */
/* transfer itself still returns its error, and the caller (Argyll layer, */
/* then the ArkTS retry) retries the operation on the recovered session. */

/* Return nz if the DDK error code is an I/O class error that session */
/* level recovery may fix. Timeouts are deliberately excluded: they are an */
/* expected outcome of several i1Pro3 operations (event waits, scan end) */
/* and a timed out transfer leaves the pipe usable. */
static int ohos_is_io_error(int32_t drv) {
	/* OHOS changes: the DDK proxy returns raw negative codes at runtime */
	/* (-1 wedged-after-timeout, -3 permanent endpoint stall) which are */
	/* NOT the documented enum values (USB_DDK_IO_FAILED is 27400003 but */
	/* -3 is what actually comes back). Treat any negative return as an */
	/* I/O class error; timeouts (27400004) are positive and excluded. */
	if (drv < 0)
		return 1;
	switch (drv) {
		case USB_DDK_INVALID_OPERATION:	/* 27400002: DDK service connection broken */
		case USB_DDK_IO_FAILED:			/* 27400003 */
			return 1;
		default:
			return 0;
	}
}

/* Serialize recovery: a failed bulk read and a failed control transfer */
/* from the i1Pro3 trigger thread may both try to recover at the same time. */
static amutex_static(ohos_recover_lock);

/* Minimum interval between recovery attempts. If the hardware is truly */
/* gone, recovery would otherwise add big sleeps to every failing transfer; */
/* and if the bulk read and the trigger thread's control write both fail on */
/* the same stall, one recovery serves both. */
#define USBIO_OHOS_RECOVER_MIN_INTERVAL 1000	/* msec */

/* Level 1 recovery: release all claimed interfaces and re-claim them. */
/* Return icom error. */
static int ohos_usb_reclaim(icoms *p) {
	struct usb_idevice *usbd = p->usbd;
	int i, nclaimed = 0;
	int32_t drv;

	/* Release all claimed interfaces */
	for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
		if (usbd->ifaceHandle[i] != 0) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: releasing iface %d handle 0x%llx",
				i, (unsigned long long)usbd->ifaceHandle[i]);
			OH_Usb_ReleaseInterface(usbd->ifaceHandle[i]);
			usbd->ifaceHandle[i] = 0;
		}
	}
	usbd->nclaimed = 0;

	/* Give the HDI driver time to tear the pipes down, and the device */
	/* time to clear its endpoint state. */
	msec_sleep(200);

	/* Re-claim every interface that has end points in use */
	for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
		uint64_t handle;
		int ifaceno;

		if (!p->ep[i].valid)
			continue;
		ifaceno = p->ep[i].ifaceno;
		if (ifaceno < 0 || ifaceno >= USBIO_OHOS_MAX_EP)
			continue;
		if (usbd->ifaceHandle[ifaceno] != 0)
			continue;	/* Already claimed */

		if ((drv = OH_Usb_ClaimInterface(usbd->deviceId,
		    (uint8_t)ifaceno, &handle)) != USB_DDK_SUCCESS) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: re-claim iface %d (dev 0x%llx) failed with %d",
				ifaceno, (unsigned long long)usbd->deviceId, drv);
			/* Leave the state clean: release whatever was re-claimed */
			for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
				if (usbd->ifaceHandle[i] != 0) {
					OH_Usb_ReleaseInterface(usbd->ifaceHandle[i]);
					usbd->ifaceHandle[i] = 0;
				}
			}
			usbd->nclaimed = 0;
			return ICOM_SYS;
		}
		usbd->ifaceHandle[ifaceno] = handle;
		nclaimed++;
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: re-claimed iface %d, handle 0x%llx",
			ifaceno, (unsigned long long)handle);
	}
	usbd->nclaimed = nclaimed;

	if (nclaimed <= 0) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: no interfaces re-claimed (ep table empty?)");
		return ICOM_SYS;
	}
	return ICOM_OK;
}

/* Level 2 recovery: restart the whole DDK session and re-find the device */
/* by VID/PID, then re-claim the interfaces. Return icom error. */
static int ohos_usb_session_restart(icoms *p) {
	struct usb_idevice *usbd = p->usbd;
	struct Usb_DeviceArray devices;
	uint64_t ids[128];
	int i, found = 0;
	int32_t drv;

	/* Release the stale interface handles while the session may still be */
	/* valid; they become meaningless after OH_Usb_Release() anyway. */
	for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
		if (usbd->ifaceHandle[i] != 0) {
			OH_Usb_ReleaseInterface(usbd->ifaceHandle[i]);
			usbd->ifaceHandle[i] = 0;
		}
	}
	usbd->nclaimed = 0;

	OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: restarting DDK session (OH_Usb_Release/Init)");
	OH_Usb_Release();
	msec_sleep(500);		/* Let the DDK service settle */

	if ((drv = OH_Usb_Init()) != USB_DDK_SUCCESS) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: OH_Usb_Init failed with %d", drv);
		return ICOM_SYS;
	}

	devices.deviceIds = ids;
	devices.num = 128;		/* Capacity */
	if ((drv = OH_Usb_GetDevices(&devices)) != USB_DDK_SUCCESS) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: OH_Usb_GetDevices failed with %d", drv);
		return ICOM_SYS;
	}

	/* Re-find our instrument by VID/PID. The deviceId changes across a */
	/* session restart, so update usbd->deviceId in place - the icoms */
	/* copy is what all transfers use from here on. */
	for (i = 0; i < (int)devices.num; i++) {
		struct UsbDeviceDescriptor desc;

		if (OH_Usb_GetDeviceDescriptor(ids[i], &desc) != USB_DDK_SUCCESS)
			continue;
		if (desc.idVendor == usbd->vid && desc.idProduct == usbd->pid) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: re-found device %04x:%04x, deviceId 0x%llx -> 0x%llx",
				usbd->vid, usbd->pid,
				(unsigned long long)usbd->deviceId, (unsigned long long)ids[i]);
			usbd->deviceId = ids[i];
			found = 1;
			break;
		}
	}
	if (!found) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: device %04x:%04x no longer visible to DDK",
			usbd->vid, usbd->pid);
		return ICOM_SYS;
	}

	msec_sleep(100);
	return ohos_usb_reclaim(p);
}

/* Recover from an I/O class transfer failure. Each level is tried once, */
/* and attempts are throttled to one per USBIO_OHOS_RECOVER_MIN_INTERVAL. */
/* Return ICOM_OK if the session looks usable again. */
static int ohos_usb_recover(icoms *p, const char *what) {
	static unsigned int last_attempt = 0;	/* msec_time() of last attempt */
	unsigned int now;
	int rv;

	if (p->usbd == NULL)
		return ICOM_SYS;

	amutex_lock(ohos_recover_lock);

	now = msec_time();
	if (last_attempt != 0 && (int)(now - last_attempt) < USBIO_OHOS_RECOVER_MIN_INTERVAL) {
		/* A recovery just happened (e.g. the concurrently failing bulk */
		/* read or control transfer beat us to it) - the session handles */
		/* are already fresh, so just let the caller retry. */
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: throttled (%{public}s), session already recovered", what);
		amutex_unlock(ohos_recover_lock);
		return ICOM_OK;
	}
	last_attempt = now;

	OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: level 1 (release/re-claim) after %{public}s", what);
	rv = ohos_usb_reclaim(p);
	if (rv == ICOM_OK) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: level 1 OK, %d interface(s) re-claimed",
			p->usbd->nclaimed);
	} else {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: level 1 failed, trying level 2 (DDK session restart)");
		rv = ohos_usb_session_restart(p);
		if (rv == ICOM_OK)
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: level 2 OK, %d interface(s) re-claimed",
				p->usbd->nclaimed);
		else
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: recover: all recovery levels failed");
	}

	amutex_unlock(ohos_recover_lock);
	return rv;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Check a USB Vendor and product ID by reading the device descriptors, */
/* and add the device to the icoms path if it is supported. */
/* Return icom nz error code on fatal error */
static
int usb_check_and_add(
a1log *log,
icompaths *pp,	/* icompaths to add to */
unsigned long long deviceId	/* OHOS DDK device id */
) {
	int rv = ICOM_OK;
	struct UsbDeviceDescriptor desc;
	unsigned vid, pid, nep10 = 0xffff;
	int32_t drv;
	devType itype;
	struct usb_idevice *usbd = NULL;

	a1logd(log, 6, "usb_check_and_add: given deviceId 0x%llx\n", deviceId);

	/* Read the device descriptor */
	if ((drv = OH_Usb_GetDeviceDescriptor(deviceId, &desc)) != USB_DDK_SUCCESS) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_check_and_add: OH_Usb_GetDeviceDescriptor(0x%llx) failed with %d",
			(unsigned long long)deviceId, drv);
		a1logd(log, 1, "usb_check_and_add: OH_Usb_GetDeviceDescriptor failed with %d\n",drv);
		return ICOM_OK;
	}

	/* Extract the vid and pid */ 	
	vid = desc.idVendor;
	pid = desc.idProduct;

	a1logd(log, 6, "usb_check_and_add: checking vid 0x%04x, pid 0x%04x\n",vid,pid);

	/* Do a preliminary match */
	if ((itype = inst_usb_match(vid, pid, 0)) == instUnknown) {
		a1logd(log, 6 , "usb_check_and_add: instrument not reconized\n");
		return ICOM_OK;
	}

	/* Allocate an idevice so that we can fill in the end point information */
	if ((usbd = (struct usb_idevice *) calloc(sizeof(struct usb_idevice), 1)) == NULL) {
		a1loge(log, ICOM_SYS, "icoms: calloc failed!\n");
		return ICOM_SYS;
	}

	usbd->deviceId = deviceId;
	usbd->vid = vid;		/* Needed by ohos_usb_session_restart() to */
	usbd->pid = pid;		/* re-find the device after a DDK restart */
	usbd->nconfig = desc.bNumConfigurations;
	usbd->iserialno = desc.iSerialNumber;

	/* Read the first configuration descriptor and extract the */
	/* interface and end point information. */
	{
		struct UsbDdkConfigDescriptor *config = NULL;

		/* OHOS changes: the DDK's configIndex parameter is interpreted as */
		/* bConfigurationValue (verified on device: the i1Pro3 needs 1, */
		/* index 0 fails). Fall back to 0 for devices that use value 0. */
		if ((drv = OH_Usb_GetConfigDescriptor(deviceId, 1, &config)) != USB_DDK_SUCCESS) {
			if ((drv = OH_Usb_GetConfigDescriptor(deviceId, 0, &config)) != USB_DDK_SUCCESS) {
				OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_check_and_add: OH_Usb_GetConfigDescriptor(0x%llx) failed with %d",
					(unsigned long long)deviceId, drv);
				a1logd(log, 1, "usb_check_and_add: OH_Usb_GetConfigDescriptor failed with %d\n",drv);
				free(usbd);
				return ICOM_OK;
			}
		}
		usbd->nifce = config->configDescriptor.bNumInterfaces;
		usbd->config = config->configDescriptor.bConfigurationValue;
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_check_and_add: config %u, %u interface(s)",
			(unsigned)usbd->config, (unsigned)usbd->nifce);

		{
			int ii, nep = 0;
			for (ii = 0; ii < (int)config->configDescriptor.bNumInterfaces; ii++) {
				struct UsbDdkInterface *ifc = &config->interface[ii];
				int as;
				for (as = 0; as < (int)ifc->numAltsetting; as++) {
					struct UsbDdkInterfaceDescriptor *ids = &ifc->altsetting[as];
					int ej;
					for (ej = 0; ej < (int)ids->interfaceDescriptor.bNumEndpoints; ej++) {
						struct UsbDdkEndpointDescriptor *eps = &ids->endPoint[ej];
						int ad = eps->endpointDescriptor.bEndpointAddress;
						int ifaceno = ids->interfaceDescriptor.bInterfaceNumber;

						nep++;
						nep10 = nep;
						if (ifaceno >= 0 && ifaceno < USBIO_OHOS_MAX_EP
						 && ad >= 0 && ad < 256) {
							usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].valid = 1;
							usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].addr = ad;
							usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].packetsize
							  = eps->endpointDescriptor.wMaxPacketSize;
							usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].type
							  = eps->endpointDescriptor.bmAttributes & IUSB_ENDPOINT_TYPE_MASK;
							usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].ifaceno = ifaceno;
							a1logd(log, 6, "set ep ad 0x%x packetsize %d type %d\n",
								ad, usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].packetsize,
								usbd->ep[((ad >> 3) & 0x10) + (ad & 0x0f)].type);
						}
					}
				}
			}
		}
		OH_Usb_FreeConfigDescriptor(config);
	}

	if (nep10 == 0xffff) {			/* Hmm. Failed to find number of end points */
		a1logd(log, 1, "usb_check_and_add: failed to find number of end points\n");
		free(usbd);
		return ICOM_OK;
	}

	a1logd(log, 6, "usb_check_and_add: found nep10 %d\n",nep10);

	/* Found a known instrument ? */
	if ((itype = inst_usb_match(vid, pid, nep10)) != instUnknown) {
		char pname[400];

		a1logd(log, 2, "usb_check_and_add: found instrument vid 0x%04x, pid 0x%04x\n",vid,pid);

		/* Create a path/identification */
		sprintf(pname,"usb://0x%04x:0x%04x (%s)", vid, pid, inst_name(itype));
		if ((usbd->dpath = strdup(pname)) == NULL) {
			a1loge(log, ICOM_SYS, "usb_check_and_add: strdup path failed!\n");
			free(usbd);
			return ICOM_SYS;
		}

		/* Add the path and ep info to the list */
		if ((rv = pp->add_usb(pp, pname, vid, pid, nep10, usbd, itype)) != ICOM_OK) {
			return rv;
		}
	} else {
		free(usbd);
	}

	return ICOM_OK;
}

/* Add paths to USB connected instruments */
/* Return an icom error */
int usb_get_paths(
icompaths *p 
) {
	int rv = ICOM_OK;
	int32_t drv;
	int i;

	a1logd(p->log, 6, "usb_get_paths: about to look through DDK devices:\n");

	/* OHOS changes: (re-)initialise the DDK on every enumeration. */
	/* OH_Usb_Init() is idempotent, and the DDK session may have been */
	/* torn down since the last enumeration (e.g. the i1probe diagnostic */
	/* calls OH_Usb_Release() when it finishes), so a static "already */
	/* inited" guard is not safe. */
	if ((drv = OH_Usb_Init()) != USB_DDK_SUCCESS) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_get_paths: OH_Usb_Init failed with %d (need ohos.permission.ACCESS_DDK_USB ?)", drv);
		a1loge(p->log, ICOM_SYS, "usb_get_paths: OH_Usb_Init failed with %d (need ohos.permission.ACCESS_DDK_USB ?)\n",drv);
		return ICOM_SYS;
	}

	{
		struct Usb_DeviceArray devices;
		uint64_t ids[128];	/* OHOS changes: the DDK copies the device id's into a */
							/* caller provided array (verified on device), with */
							/* devices.num as capacity in / actual count out. */

		devices.deviceIds = ids;
		devices.num = 128;		/* Capacity */
		if ((drv = OH_Usb_GetDevices(&devices)) != USB_DDK_SUCCESS) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_get_paths: OH_Usb_GetDevices failed with %d", drv);
			a1loge(p->log, ICOM_SYS, "usb_get_paths: OH_Usb_GetDevices failed with %d\n",drv);
			return ICOM_SYS;
		}
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_get_paths: %u USB device(s) visible to DDK", (unsigned)devices.num);

		for (i = 0; i < (int)devices.num; i++) {
			a1logd(p->log, 8, "usb_get_paths: about to check deviceId 0x%llx\n", (unsigned long long)ids[i]);
			if ((rv = usb_check_and_add(p->log, p, ids[i])) != ICOM_OK) {
				break;
			}
		}
	}

	a1logd(p->log, 8, "usb_get_paths: returning %d paths and ICOM_OK\n",p->ndpaths[dtix_combined]);
	return rv;
}

/* Copy usb_idevice contents from icompaths to icom */
/* return icom error */
int usb_copy_usb_idevice(icoms *d, icompath *s) {
	int i;
	if (s->usbd == NULL) { 
		d->usbd = NULL;
		return ICOM_OK;
	}
	if ((d->usbd = calloc(sizeof(struct usb_idevice), 1)) == NULL) {
		a1loge(d->log, ICOM_SYS, "usb_copy_usb_idevice: malloc\n");
		return ICOM_SYS;
	}
	/* Struct copy (includes deviceId, ifaceHandle, ep info, etc.) */
	*d->usbd = *s->usbd;
	/* Deep copy the string pointers */
	if (s->usbd->dpath != NULL
	 && (d->usbd->dpath = strdup(s->usbd->dpath)) == NULL) {
		a1loge(d->log, ICOM_SYS, "usb_copy_usb_idevice: malloc\n");
		return ICOM_SYS;
	}
	if (s->usbd->SerialNumber != NULL
	 && (d->usbd->SerialNumber = strdup(s->usbd->SerialNumber)) == NULL) {
		a1loge(d->log, ICOM_SYS, "usb_copy_usb_idevice: malloc\n");
		return ICOM_SYS;
	}

	/* Copy the endpoint and config info into the icoms structure itself - */
	/* the EPINFO() macro and usb_open_port()/icoms_usb_rw() use the icoms */
	/* ep[] array, not the usbd->ep[] copy. (This mirrors usbio_lx.c; without */
	/* it the endpoint table stays all-zero and no interface gets claimed.) */
	d->nconfig = s->usbd->nconfig;
	d->nifce = s->usbd->nifce;
	d->config = s->usbd->config;
	for (i = 0; i < 32; i++)
		d->ep[i] = s->usbd->ep[i];		/* Struct copy */
	return ICOM_OK;
}

/* Cleanup and then free a usb dev entry */
void usb_del_usb_idevice(struct usb_idevice *usbd) {

	if (usbd == NULL)
		return;

	if (usbd->dpath != NULL)
		free(usbd->dpath);
	if (usbd->SerialNumber != NULL)
		free(usbd->SerialNumber);
	free(usbd);
}

/* Cleanup any USB specific icoms state */
void usb_del_usb(icoms *p) {

	usb_del_usb_idevice(p->usbd);
}

/* Close an open USB port */
/* If we don't do this, the port and/or the device may be left in an unusable state. */
void usb_close_port(icoms *p) {

	a1logd(p->log, 6, "usb_close_port: called\n");

	if (p->is_open && p->usbd != NULL) {
		int i;

		/* Release all the claimed interfaces */
		for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
			if (p->usbd->ifaceHandle[i] != 0) {
				OH_Usb_ReleaseInterface(p->usbd->ifaceHandle[i]);
				p->usbd->ifaceHandle[i] = 0;
			}
		}
		p->usbd->nclaimed = 0;

		a1logd(p->log, 6, "usb_close_port: usb port has been released and closed\n");
	}
	p->is_open = 0;

	/* Find it and delete it from our static cleanup list */
	usb_delete_from_cleanup_list(p);
}

/* Open a USB port for all our uses. */
/* This always re-opens the port */
/* return icom error */
static int usb_open_port(
icoms *p,
int    config,		/* Configuration number */
int    wr_ep,		/* Write end point */
int    rd_ep,		/* Read end point */
icomuflags usbflags,/* Any special handling flags */
int retries,		/* > 0 if we should retry set_configuration (100msec) */ 
char **pnames		/* List of process names to try and kill before opening */
) {
	int rv;
	int i;
	int32_t drv;

	a1logd(p->log, 8, "usb_open_port: Make sure USB port is open, retries %d\n",retries);
	OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: dev 0x%llx config %d wr_ep 0x%x rd_ep 0x%x flags 0x%x",
		p->usbd != NULL ? (unsigned long long)p->usbd->deviceId : 0ULL,
		config, wr_ep, rd_ep, (unsigned)usbflags);

	if (p->is_open)
		p->close_port(p);

	/* Make sure the port is open */
	if (!p->is_open) {

		if (p->usbd == NULL) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: no USB device context (usbd == NULL)");
			a1loge(p->log, ICOM_SYS, "usb_open_port: usbd is NULL\n");
			return ICOM_SYS;
		}

		if (config != 1) {
			/* Nothing currently needs it, so we haven't implemented it yet... */
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: cant handle config %d", config);
			a1loge(p->log, ICOM_NOTS, "usb_open_port: native driver cant handle config %d\n",config);
			return ICOM_NOTS;
		}

		p->uflags = usbflags;

		/* The DDK selects the default configuration on claim, */
		/* so there is nothing to do for the configuration. */
		p->cconfig = 1;

		/* Claim all the interfaces that have end points in use. */
		/* (The i1Pro3 has its bulk end points spread over more */
		/* than one interface.) */
		/* OHOS changes: dump the endpoint table to hilog, so that an */
		/* empty/garbled table (enumeration problem) is distinguishable */
		/* from a ClaimInterface() failure on the device. */
		for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
			if (p->ep[i].valid) {
				OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: ep[%d] addr 0x%x pkt %d type %d iface %d",
					i, p->ep[i].addr, p->ep[i].packetsize, p->ep[i].type, p->ep[i].ifaceno);
			}
		}
		p->usbd->nclaimed = 0;
		for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
			uint64_t handle;		/* OHOS changes: use uint64_t for the DDK API */
			int ifaceno;

			if (!p->ep[i].valid)
				continue;
			ifaceno = p->ep[i].ifaceno;
			if (ifaceno < 0 || ifaceno >= USBIO_OHOS_MAX_EP)
				continue;
			if (p->usbd->ifaceHandle[ifaceno] != 0)
				continue;	/* Already claimed */

			if ((drv = OH_Usb_ClaimInterface(p->usbd->deviceId,
			    (uint8_t)ifaceno, &handle)) != USB_DDK_SUCCESS) {
				OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: ClaimInterface(dev 0x%llx, iface %d) failed with %d",
					(unsigned long long)p->usbd->deviceId, ifaceno, drv);
				a1loge(p->log, ICOM_SYS,
					"usb_open_port: Claiming USB port '%s' interface %d failed with %d\n",
					p->usbd->dpath, ifaceno, drv);
				usb_close_port(p);
				return ICOM_SYS;
			}
			p->usbd->ifaceHandle[ifaceno] = handle;
			p->usbd->nclaimed++;
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: claimed iface %d, handle 0x%llx",
				ifaceno, (unsigned long long)handle);
			a1logd(p->log, 6, "usb_open_port: Claimed USB port '%s' interface %d handle 0x%llx\n",
				p->usbd->dpath, ifaceno, handle);
		}
		if (p->usbd->nclaimed <= 0) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: no interfaces to claim for dev 0x%llx (ep table empty?)",
				(unsigned long long)p->usbd->deviceId);
			a1loge(p->log, ICOM_SYS, "usb_open_port: no interfaces to claim for '%s'\n",p->usbd->dpath);
			return ICOM_SYS;
		}

		/* Set "serial" coms values */
		p->wr_ep = wr_ep;
		p->rd_ep = rd_ep;
		p->rd_qa = p->EPINFO(rd_ep).packetsize;
		if (p->rd_qa == 0)
			p->rd_qa = 8;
		a1logd(p->log, 8, "usb_open_port: 'serial' read quanta = packet size = %d\n",p->rd_qa);

		p->is_open = 1;
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: usb_open_port: open OK, %d interface(s) claimed, rd_qa %d",
			p->usbd->nclaimed, p->rd_qa);
		a1logd(p->log, 8, "usb_open_port: USB port is now open\n");

		/* OHOS: The USB serial number string could be read with a */
		/* standard GET_DESCRIPTOR control request, but the i1Pro3 */
		/* does not need it, so it is skipped here. */
	}

	/* Install the cleanup signal handlers, and add to our cleanup list */
	usb_install_signal_handlers(p);

	return ICOM_OK;
}

/*  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Our universal USB transfer function (synchronous). */
/* Note that control transfers are handled directly by icoms_usb_control_msg() */
/* below - icoms_usb_transaction() only sees bulk and interrupt transfers. */
static int icoms_usb_transaction(
	icoms *p,
	usb_cancelt *cancelt,
	int *transferred,
	icom_usb_trantype ttype,	/* transfer type */
	unsigned char endpoint,		/* 0x80 for control write, 0x00 for control read */
	unsigned char *buffer,
	int length,
	unsigned int timeout		/* In msec */
) {
	int reqrv = ICOM_OK;
	unsigned long long handle;
	struct usb_idevice *usbd = p->usbd;
	UsbDeviceMemMap *devMmap = NULL;
	UsbRequestPipe pipe;
	int32_t drv;
	int xferred = 0;	/* Bytes actually transferred */
	int dirin = (endpoint & IUSB_ENDPOINT_DIR_MASK) == IUSB_ENDPOINT_IN;

	in_usb_rw++;
	a1logd(p->log, 8, "icoms_usb_transaction: req type 0x%x ep 0x%x size %d to %d\n",ttype,endpoint,length, timeout);

	if (transferred != NULL)
		*transferred = 0;

	if (!p->is_open) {
		in_usb_rw--;
		a1loge(p->log, ICOM_SYS, "icoms_usb_transaction: device not open\n");
		return ICOM_SYS;
	}

	if (ttype == icom_usb_trantype_command) {
		/* Control transfers are done via icoms_usb_control_msg() */
		in_usb_rw--;
		a1logv(p->log, 1, "icoms_usb_transaction: control transfers not supported here\n");
		return ICOM_NOTS;
	}

	/* Find the claimed interface handle for this endpoint */
	if ((handle = ohos_ep_handle(p, endpoint)) == 0) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_transaction: no claimed interface for ep 0x%x", endpoint);
		in_usb_rw--;
		a1loge(p->log, ICOM_SYS, "icoms_usb_transaction: no claimed interface for ep 0x%x\n",endpoint);
		return ICOM_SYS;
	}

	if (cancelt != NULL) {
		amutex_lock(cancelt->cmtx);
		cancelt->hcancel = (void *)1;	/* Mark transaction in progress */
		cancelt->state = 1;
		amutex_unlock(cancelt->condx);	/* Signal any thread waiting for IO start */
		amutex_unlock(cancelt->cmtx);
	}

	/* OHOS changes: pace back to back transfers. The DDK HDI has been */
	/* observed to wedge (a transfer blocking for its entire timeout, then */
	/* the endpoint permanently stalling) when transfers arrive in a tight */
	/* burst. A couple of msec per transfer is negligible against the */
	/* 1-2 sec a measurement takes. (Control transfers are already paced */
	/* by the i1Pro3 driver, which sleeps 1msec before each command.) */
	msec_sleep(2);

	/* Create a device memory map for the transfer buffer */
	if ((drv = OH_Usb_CreateDeviceMemMap(usbd->deviceId, (size_t)length, &devMmap)) != USB_DDK_SUCCESS) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_transaction: CreateDeviceMemMap(dev 0x%llx, %d) failed with %d",
			(unsigned long long)usbd->deviceId, length, drv);
		a1loge(p->log, ICOM_SYS, "icoms_usb_transaction: OH_Usb_CreateDeviceMemMap failed with %d\n",drv);
		if (ohos_is_io_error(drv))
			ohos_usb_recover(p, "CreateDeviceMemMap");
		reqrv = ICOM_SYS;
		goto done;
	}

	/* For a write, copy the data into the mapped buffer */
	if (!dirin)
		memcpy(devMmap->address, buffer, (size_t)length);

	/* Setup the pipe and send the request */
	pipe.interfaceHandle = handle;
	/* OHOS changes: timeout policy. A DDK-level timeout doesn't just fail */
	/* the transfer — it wedges the pipe (subsequent transfers return -3), */
	/* and both field failures were exactly this: the i1pro3 switch/event */
	/* thread PARKS an interrupt read on ep 0x83 with a very long timeout; */
	/* when the cap fired at 30s/600s the pipe wedged and killed the */
	/* in-flight measurement. So: interrupt transfers keep the caller's */
	/* (long) timeout — the event-wait code treats timeout as benign */
	/* (BUTTONTIMEOUT); bulk transfers are capped at 60s (a real transfer */
	/* takes ms..seconds), and any resulting wedge is repaired by */
	/* ohos_usb_recover() + the caller's retry. */
	if (ttype == icom_usb_trantype_interrutpt)
		pipe.timeout = 0xFFFFFFFF;	/* ~49 days: never fires */
	else
		pipe.timeout = timeout > 60000 ? 60000 : timeout;
	pipe.endpoint = endpoint;
	drv = OH_Usb_SendPipeRequest(&pipe, devMmap);

	/* OHOS changes: capture transferedLength and copy the read data out */
	/* BEFORE destroying the memmap. The original code destroyed devMmap */
	/* and then dereferenced devMmap->transferedLength, which crashed on */
	/* the first successful bulk/interrupt transfer. */
	if (drv == USB_DDK_SUCCESS) {
		xferred = (int)devMmap->transferedLength;
		if (xferred > length)	/* Paranoia - never overflow the caller's buffer */
			xferred = length;
		/* For a read, copy the data back out of the mapped buffer */
		if (dirin && xferred > 0)
			memcpy(buffer, devMmap->address, (size_t)xferred);
	}

	OH_Usb_DestroyDeviceMemMap(devMmap);
	devMmap = NULL;

	if (drv != USB_DDK_SUCCESS) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_transaction: SendPipeRequest(ep 0x%x, %d bytes) failed with %d",
			endpoint, length, drv);
		a1logd(p->log, 1, "icoms_usb_transaction: OH_Usb_SendPipeRequest ep 0x%x failed with %d\n",endpoint,drv);
		if (drv == USB_DDK_TIMEOUT)
			reqrv = ICOM_TO;
		else if (dirin)
			reqrv = ICOM_USBR;
		else
			reqrv = ICOM_USBW;
		/* On an I/O class error the endpoint may be permanently stalled */
		/* (the DDK has no clearhalt): recover the session so that the */
		/* caller's retry has a chance of succeeding. */
		if (ohos_is_io_error(drv))
			ohos_usb_recover(p, "SendPipeRequest");
		goto done;
	}

	if (transferred != NULL)
		*transferred = xferred;

	/* Requested size wasn't transferred ? */
	if (xferred != length)
		reqrv |= ICOM_SHORT;

done:;
	if (devMmap != NULL)
		OH_Usb_DestroyDeviceMemMap(devMmap);
	if (cancelt != NULL) {
		amutex_lock(cancelt->cmtx);
		cancelt->hcancel = (void *)NULL;
		if (cancelt->state == 0)
			amutex_unlock(cancelt->condx);
		cancelt->state = 2;
		amutex_unlock(cancelt->cmtx);
	}

	if (in_usb_rw < 0)
		exit(0);

	in_usb_rw--;

	a1logd(p->log, 8, "coms_usb_transaction: returning err 0x%x and %d bytes\n",reqrv, transferred != NULL ? *transferred : 0);

	return reqrv;
}

/* Return error icom error code */
static int icoms_usb_control_msg(
icoms *p,
int *transferred,
int requesttype, int request,
int value, int index, unsigned char *bytes, int size, 
int timeout) {
	int reqrv = ICOM_OK;
	int dirin = (requesttype & IUSB_REQ_DIR_MASK) == IUSB_REQ_DEV_TO_HOST;
	struct UsbControlRequestSetup setup;
	unsigned long long handle;
	int32_t drv;
	int i;

	a1logd(p->log, 8, "icoms_usb_control_msg: type 0x%x req 0x%x size %d\n",requesttype,request,size);

	if (transferred != NULL)
		*transferred = 0;

	if (!p->is_open) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_control_msg: device not open");
		a1loge(p->log, ICOM_SYS, "icoms_usb_control_msg: device not open\n");
		return ICOM_SYS;
	}

	/* Find a claimed interface handle - the vendor requests in the */
	/* i1Pro3 protocol are directed at the device, so any handle works. */
	handle = 0;
	for (i = 0; i < USBIO_OHOS_MAX_EP; i++) {
		if (p->usbd->ifaceHandle[i] != 0) {
			handle = p->usbd->ifaceHandle[i];
			break;
		}
	}
	if (handle == 0) {
		OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_control_msg: no claimed interface");
		a1loge(p->log, ICOM_SYS, "icoms_usb_control_msg: no claimed interface\n");
		return ICOM_SYS;
	}

	/* Setup the control request header */
	setup.bmRequestType = (uint8_t)(requesttype & 0xff);
	setup.bRequest = (uint8_t)(request & 0xff);
	setup.wValue = (uint16_t)(value & 0xffff);
	setup.wIndex = (uint16_t)(index & 0xffff);
	setup.wLength = (uint16_t)(size & 0xffff);

	if (dirin) {
		uint32_t dataLen = (uint32_t)size;
		drv = OH_Usb_SendControlReadRequest(handle, &setup, (uint32_t)timeout, bytes, &dataLen);
		if (drv != USB_DDK_SUCCESS) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_control_msg: read req 0x%x type 0x%x size %d failed with %d",
				request, requesttype, size, drv);
			a1logd(p->log, 1, "icoms_usb_control_msg: read failed with %d\n",drv);
			if (drv == USB_DDK_TIMEOUT)
				reqrv = ICOM_TO;
			else
				reqrv = ICOM_USBR;
			/* I/O class error: the session may be broken - recover it */
			/* (once, throttled) so the caller's retry can succeed. */
			if (ohos_is_io_error(drv))
				ohos_usb_recover(p, "SendControlReadRequest");
		} else {
			if (transferred != NULL)
				*transferred = (int)dataLen;
			if ((int)dataLen != size)
				reqrv |= ICOM_SHORT;
		}
	} else {
		drv = OH_Usb_SendControlWriteRequest(handle, &setup, (uint32_t)timeout, bytes, (uint32_t)size);
		if (drv != USB_DDK_SUCCESS) {
			OH_LOG_ERROR(LOG_APP, "usbio_ohos: icoms_usb_control_msg: write req 0x%x type 0x%x size %d failed with %d",
				request, requesttype, size, drv);
			a1logd(p->log, 1, "icoms_usb_control_msg: write failed with %d\n",drv);
			if (drv == USB_DDK_TIMEOUT)
				reqrv = ICOM_TO;
			else
				reqrv = ICOM_USBW;
			if (ohos_is_io_error(drv))
				ohos_usb_recover(p, "SendControlWriteRequest");
		} else {
			if (transferred != NULL)
				*transferred = size;
		}
	}

	a1logd(p->log, 8, "icoms_usb_control_msg: returning err 0x%x and %d bytes\n",reqrv, transferred != NULL ? *transferred : 0);
	return reqrv;
}

/*  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Time out error return value */

#define USBIO_ERROR_TIMEOUT	-ETIMEDOUT

/*  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Cancel i/o in another thread */
int icoms_usb_cancel_io(
	icoms *p,
	usb_cancelt *cancelt
) {
	int rv = ICOM_OK;
	a1logd(p->log, 8, "icoms_usb_cancel_io called\n");

	/* OHOS: transfers are synchronous, so there is nothing to cancel */
	/* in progress - the timeout parameter bounds each transfer. */
	amutex_lock(cancelt->cmtx);
	if (cancelt->hcancel != NULL)
		rv = ICOM_OK;
	amutex_unlock(cancelt->cmtx);

	return rv;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Reset and end point data toggle to 0 */
int icoms_usb_resetep(
	icoms *p,
	int ep					/* End point address */
) {
	/* OHOS: no DDK equivalent - no-op */
	return ICOM_OK;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Clear a halt on an end point */
int icoms_usb_clearhalt(
	icoms *p,
	int ep					/* End point address */
) {
	/* OHOS: no DDK equivalent - no-op */
	return ICOM_OK;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
