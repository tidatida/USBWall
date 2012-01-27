/*
** File procfs_iface.c for project usbwall
**
** LACSC - ECE PARIS Engineering school 
**
** Made by Philippe THIERRY
** Login   <phil@reseau-libre.net>
**
** Started on  jeu. 19 janv. 2012 10:34:39 CET Philippe THIERRY
**
** Copyright (C) 2009 - Philippe THIERRY
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
/*
** \file procfs_iface.c
**
** Brief documentation on one line
** Detailed documentation
** On multiple lines...
**
** \author Philippe THIERRY
**
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "procfs_iface.h"
#include "trace.h"
#include "usbwall.h"
#include "keylist.h"
#include "keylist_info.h"

#define USBWALL_PROC_STATUS_BUFFER_SIZE 256
#define USBWALL_PROC_RELEASE_BUFFER_SIZE 16

static struct proc_dir_entry* usbwalldir = NULL;
static struct proc_dir_entry* usbwallkeyctrl = NULL;
static struct proc_dir_entry* usbwallstatus = NULL;
static struct proc_dir_entry* usbwallrelease = NULL;

static char status_buffer[USBWALL_PROC_STATUS_BUFFER_SIZE];
static char release_buffer[USBWALL_PROC_RELEASE_BUFFER_SIZE];

static int nb_element = 0;

/*!
 ** \fn write_func
 ** 
 ** \param file 
 ** \param buffer 
 ** \param count 
 ** \param data 
 ** 
 ** \return 
 */
static int usbwall_keyctrl_write(struct file* file,
                                 const char* buffer,
                                 unsigned long count,
                                 void* data)
{
    int len;
    procfs_info_t u_keyinfo;
   
    struct internal_token_info *internal_keyinfo = NULL;
    /* struct fb_data_t *fb_data = (struct fb_data_t *)data; */

    internal_keyinfo = kmalloc(sizeof(struct internal_token_info),GFP_KERNEL);
    if (internal_keyinfo == NULL) {
        return -ENOMEM;
    }
    /* MOD_INC_USE_COUNT; */
    if(count > sizeof(procfs_info_t))
        len = sizeof(procfs_info_t);
    else
        len = count;
    DBG_TRACE("reading %ld len from userspace", count);
    if(copy_from_user(u_keyinfo.buffer, buffer, len)) {
        /* MOD_DEC_USE_COUNT; */
        return -EFAULT;
    }
    u_keyinfo.buffer[len] = '\0';
    DBG_TRACE("reading: vendor: %x, product: %x, serial: %s",
	      u_keyinfo.info.idVendor,
              u_keyinfo.info.idProduct,
              u_keyinfo.info.idSerialNumber);

    /* copy information to internal_keyinfo */
    internal_keyinfo->info.idVendor = u_keyinfo.info.idVendor;
    internal_keyinfo->info.idProduct = u_keyinfo.info.idProduct;
    strcpy(internal_keyinfo->info.idSerialNumber, u_keyinfo.info.idSerialNumber);

    if ((u_keyinfo.info.keyflags & USBWALL_KEY_ADD)) {
      DBG_TRACE("n°%d adding key %s to whitelist", nb_element, u_keyinfo.info.idSerialNumber);
      //key_add(&(internal_keyinfo));
      key_add(internal_keyinfo);
      nb_element++;
    } else if ((u_keyinfo.info.keyflags & USBWALL_KEY_DEL)) {
      DBG_TRACE("deleting key %s to whitelist", u_keyinfo.info.idSerialNumber);
      //key_del(&(internal_keyinfo));
      key_del(internal_keyinfo);
      nb_element--;
    }

    /* MOD_DEC_USE_COUNT; */

    return len;
}

/*!
 ** \brief usbwall_status_read
 ** 
 ** \param page the kernel mapped memory page in which the data should be copied
 ** \param start unused
 ** \param off the current string offset asked by the userspace (if read in multiple times)
 ** \param count the asked bytes count to read
 ** \param eof EOF signal pointer to be setted
 ** \param data unused
 ** 
 ** \return the number of effectively copied bytes
 */
int usbwall_status_read(char *page,
                        char **start,
                        off_t off,
                        int count,
                        int *eof,
                        void *data)
{
   int is_eof = 0;

   DBG_TRACE("entering status read, asking for reading %d bytes. Current offset is %ld", count, off);
   /*
   ** test buffer by now
   */
   sprintf(status_buffer, "USBWall module release %s\n", USBWALL_MODVERSION);
   print_keylist(status_buffer);
   if (off >= strlen(status_buffer)) {
     DBG_TRACE("offset greater than string size, leaving");
     count = 0;
     is_eof = 1;
     goto end;
   }
   if (count > (strlen(status_buffer) - off)) {
     count = (strlen(status_buffer) - off + 1);
     is_eof = 1;
   }
   snprintf(&(page[off]), count, &(status_buffer[off]));

end:
   if (is_eof) {
     DBG_TRACE("EOF=1");
     /* update file offset */
      *eof = 1;
   } else {
      *eof = 0;
   }
   DBG_TRACE("end of function. Returning %d", count);
   return count;
}

/*!
 ** \brief usbwall_release_read
 **
 ** Return the module release identifier, in order to be checked by the libusbwall to guarantee the interoperability
 ** 
 ** \param page the kernel mapped memory page in which the data should be copied
 ** \param start unused
 ** \param off the current string offset asked by the userspace (if read in multiple times)
 ** \param count the asked bytes count to read
 ** \param eof EOF signal pointer to be setted
 ** \param data unused
 ** 
 ** \return the number of effectively copied bytes
 */
int usbwall_release_read(char *page,
                         char **start,
                         off_t off,
                         int count,
                         int *eof,
                         void *data)
{
   int is_eof = 0;

   DBG_TRACE("entering release read, asking for reading %d bytes. Current offset is %ld", count, off);
   /*
   ** return the release code
   */
   sprintf(release_buffer, "%d", USBWALL_MAJOR << 16 | USBWALL_MEDIUM << 8 | USBWALL_CURRENT);
   if (off >= strlen(release_buffer)) {
     DBG_TRACE("offset greater than string size, leaving");
     count = 0;
     is_eof = 1;
     goto end;
   }
   if (count > (strlen(release_buffer) - off)) {
     count = (strlen(release_buffer) - off + 1);
     is_eof = 1;
   }
   snprintf(&(page[off]), count, &(release_buffer[off]));

end:
   if (is_eof) {
     DBG_TRACE("EOF=1");
     /* update file offset */
      *eof = 1;
   } else {
      *eof = 0;
   }
   DBG_TRACE("end of function. Returning %d", count);
   return count;
}


/*!
 ** \fn usbwall_proc_init initialize the usbwall procfs itnerface
 ** 
 ** \return 1 if init failed, or 0.
 */
int usbwall_proc_init()
{
    usbwalldir = proc_mkdir("usbwall", NULL);
    if (usbwalldir == NULL) {
	goto fail_proc_mkdir;
    }
    usbwallkeyctrl = create_proc_entry("key_ctrl", 0600, usbwalldir);
    if (usbwallkeyctrl == NULL) {
	goto fail_proc_entry;
    }
    usbwallstatus = create_proc_entry("status", 0400, usbwalldir);
    if (usbwallstatus == NULL) {
	goto fail_proc_entry_2;
    }
    usbwallrelease = create_proc_entry("release", 0400, usbwalldir);
    if (usbwallrelease == NULL) {
	goto fail_proc_entry_3;
    }


    usbwallkeyctrl->write_proc = usbwall_keyctrl_write;
    usbwallstatus->read_proc = usbwall_status_read;
    usbwallrelease->read_proc = usbwall_release_read;
    return 0;

/* failure management - std linux usage */
fail_proc_entry_3:
    remove_proc_entry("release", usbwalldir);
fail_proc_entry_2:
    remove_proc_entry("status", usbwalldir);
fail_proc_entry:
    remove_proc_entry("usbwall", NULL);
fail_proc_mkdir:
    return 1;
}

/*!
 ** \fn usbwall_proc_release release the usbwall procfs interface
 */
void usbwall_proc_release()
{
    remove_proc_entry("release", usbwalldir);
    remove_proc_entry("status", usbwalldir);
    remove_proc_entry("key_ctrl", usbwalldir);
    remove_proc_entry("usbwall", NULL);
}
