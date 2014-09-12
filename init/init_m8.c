/*
   Copyright (c) 2013, The Linux Foundation. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
   BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
   OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
   IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>

#include "vendor_init.h"
#include "property_service.h"
#include "log.h"
#include "util.h"

void cdma_properties(char cdma_subscription[],
                     char default_network[],
                     char operator_numeric[],
                     char operator_alpha[]);

void vendor_load_properties()
{
    char platform[PROP_VALUE_MAX];
    char bootmid[PROP_VALUE_MAX];
    char device[PROP_VALUE_MAX];
    char devicename[PROP_VALUE_MAX];
    int rc;

    rc = property_get("ro.board.platform", platform);
    if (!rc || strncmp(platform, ANDROID_TARGET, PROP_VALUE_MAX))
        return;

    property_get("ro.boot.mid", bootmid);

    if (strstr(bootmid, "0P6B20000")) {
        /* m8vzw (m8wl) */
        cdma_properties("0", "10", "310012", "Verizon");
        property_set("ro.build.fingerprint", "htc/HTCOneM8vzw/htc_m8wl:4.4.3/KTU84L/362279.2:user/release-keys");
        property_set("ro.build.description", "2.21.605.2 CL362279 release-keys");
        property_set("ro.product.device", "htc_m8wl");
        property_set("ro.build.product", "htc_m8wl");
        property_set("ro.ril.vzw.feature", "1");
        property_set("ro.ril.oem.ecclist", "911,*911,#911");
        property_set("ro.ril.enable.a52", "0");
        property_set("ro.ril.enable.dtm", "0");
        property_set("ro.ril.gprsclass", "12");
        property_set("ro.ril.att.feature", "0");
        property_set("ro.ril.enable.managed.roaming", "1");
        property_set("ro.ril.oem.show.act", "0");
        property_set("ro.ril.set.mtusize", "1428");
        property_set("ro.ril.air.enabled", "1");
        property_set("ro.ril.wp.feature", "1");
        property_set("ro.cdma.data_retry_config", "max_retries=infinite,0,0,60000,120000,480000,900000");
        property_set("ro.gsm.data_retry_config", "max_retries=infinite,0,0,60000,120000,480000,900000");
        property_set("ro.gsm.2nd_data_retry_config", "max_retries=infinite,0,0,60000,120000,480000,900000");
        property_set("ro.ril.gsm.to.lte.blind.redir", "1");
        property_set("ro.config.svlte1x", "true");
        property_set("ro.ril.def.agps.mode", "6");
    } else if (strstr(bootmid, "0P6B70000")) {
        /* m8spr (m8whl) */
        cdma_properties("1", "8", "310120", "Sprint");
        property_set("ro.build.fingerprint", "htc/sprint_wwe/htc_m8whl:4.4.2/KOT49H/326598.8:user/release-keys");
        property_set("ro.build.description", "1.54.651.8 CL326598 release-keys");
        property_set("ro.product.device", "m8spr");
        property_set("ro.build.product", "m8spr");
        property_set("telephony.sms.pseudo_multipart", "1");
        property_set("ro.ril.oem.ecclist", "911");
        property_set("ro.ril.enable.pre_r8fd=1", "1");
        property_set("ro.ril.enable.sdr", "0");
        property_set("ro.ril.set.mtusize", "1422");
    } else {
        /* m8 */
        property_set("ro.build.fingerprint", "htc/htc_europe/htc_m8:4.4.2/KOT49H/325784.5:user/release-keys");
        property_set("ro.build.description", "1.54.401.5 CL325784 release-keys");
        property_set("ro.product.device", "m8");
        property_set("ro.telephony.default_network", "9");
        property_set("telephony.lteOnGsmDevice", "1");
    }

    property_get("ro.product.device", device);
    ERROR("Found bootmid %s setting build properties for %s device\n", bootmid, device);
}

void cdma_properties(char default_cdma_sub[], char default_network[],
                     char operator_numeric[], char operator_alpha[])
{
    property_set("ro.telephony.default_cdma_sub", default_cdma_sub);
    property_set("ro.telephony.default_network", default_network);
    property_set("ro.cdma.home.operator.numeric", operator_numeric);
    property_set("gsm.sim.operator.numeric", operator_numeric);
    property_set("gsm.operator.numeric", operator_numeric);
    property_set("ro.cdma.home.operator.alpha", operator_alpha);
    property_set("gsm.sim.operator.alpha", operator_alpha);
    property_set("gsm.operator.alpha", operator_numeric);

    property_set("telephony.lteOnCdmaDevice", "1");
    property_set("ro.cdma.subscribe_on_ruim_ready", "true");
    property_set("ro.ril.svdo", "true");
    property_set("ro.ril.disable.fd.plmn.prefix", "23402,23410,23411,23420");
    property_set("ro.ril.enable.sdr", "0");
    property_set("ro.ril.enable.gea3", "1");
    property_set("ro.ril.enable.a53", "1");
    property_set("ro.ril.enable.r8fd=1", "1");
    property_set("persist.radio.snapshot_enabled", "1");
    property_set("persist.radio.snapshot_timer", "22");
}
