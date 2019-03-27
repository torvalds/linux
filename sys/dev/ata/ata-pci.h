/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* structure holding chipset config info */
struct ata_chip_id {
    u_int32_t           chipid;
    u_int8_t            chiprev;
    int                 cfg1;
    int                 cfg2;
    u_int8_t            max_dma;
    const char          *text;
};

#define ATA_PCI_MAX_CH	8

/* structure describing a PCI ATA controller */
struct ata_pci_controller {
    device_t            dev;
    int                 r_type1;
    int                 r_rid1;
    struct resource     *r_res1;
    int                 r_type2;
    int                 r_rid2;
    struct resource     *r_res2;
    int                 r_irq_rid;
    struct resource     *r_irq;
    void                *handle;
    const struct ata_chip_id *chip;
    int			legacy;
    int                 channels;
    int			ichannels;
    int                 (*chipinit)(device_t);
    int                 (*chipdeinit)(device_t);
    int                 (*suspend)(device_t);
    int                 (*resume)(device_t);
    int                 (*ch_attach)(device_t);
    int                 (*ch_detach)(device_t);
    int                 (*ch_suspend)(device_t);
    int                 (*ch_resume)(device_t);
    void                (*reset)(device_t);
    int                 (*setmode)(device_t, int, int);
    int                 (*getrev)(device_t, int);
    struct {
    void                (*function)(void *);
    void                *argument;
    } interrupt[ATA_PCI_MAX_CH];
    void                *chipset_data;
};

/* defines for known chipset PCI id's */
#define ATA_ACARD_ID            0x1191
#define ATA_ATP850              0x00021191
#define ATA_ATP850A             0x00041191
#define ATA_ATP850R             0x00051191
#define ATA_ATP860A             0x00061191
#define ATA_ATP860R             0x00071191
#define ATA_ATP865A             0x00081191
#define ATA_ATP865R             0x00091191

#define ATA_ACER_LABS_ID        0x10b9
#define ATA_ALI_1533            0x153310b9
#define ATA_ALI_5228            0x522810b9
#define ATA_ALI_5229            0x522910b9
#define ATA_ALI_5281            0x528110b9
#define ATA_ALI_5287            0x528710b9
#define ATA_ALI_5288            0x528810b9
#define ATA_ALI_5289            0x528910b9

#define ATA_AMD_ID              0x1022
#define ATA_AMD755              0x74011022
#define ATA_AMD756              0x74091022
#define ATA_AMD766              0x74111022
#define ATA_AMD768              0x74411022
#define ATA_AMD8111             0x74691022
#define ATA_AMD5536             0x209a1022
#define ATA_AMD_HUDSON2_S1	0x78001022
#define ATA_AMD_HUDSON2_S2	0x78011022
#define ATA_AMD_HUDSON2_S3	0x78021022
#define ATA_AMD_HUDSON2_S4	0x78031022
#define ATA_AMD_HUDSON2_S5	0x78041022
#define ATA_AMD_HUDSON2		0x780c1022

#define ATA_ADAPTEC_ID          0x9005
#define ATA_ADAPTEC_1420        0x02419005
#define ATA_ADAPTEC_1430        0x02439005

#define ATA_ATI_ID              0x1002
#define ATA_ATI_IXP200          0x43491002
#define ATA_ATI_IXP300          0x43691002
#define ATA_ATI_IXP300_S1       0x436e1002
#define ATA_ATI_IXP400          0x43761002
#define ATA_ATI_IXP400_S1       0x43791002
#define ATA_ATI_IXP400_S2       0x437a1002
#define ATA_ATI_IXP600          0x438c1002
#define ATA_ATI_IXP600_S1       0x43801002
#define ATA_ATI_IXP700          0x439c1002
#define ATA_ATI_IXP700_S1       0x43901002
#define	ATA_ATI_IXP700_S2	0x43911002
#define	ATA_ATI_IXP700_S3	0x43921002
#define	ATA_ATI_IXP700_S4	0x43931002
#define	ATA_ATI_IXP800_S1	0x43941002
#define	ATA_ATI_IXP800_S2	0x43951002

#define ATA_CENATEK_ID          0x16ca
#define ATA_CENATEK_ROCKET      0x000116ca

#define ATA_CYRIX_ID            0x1078
#define ATA_CYRIX_5530          0x01021078

#define ATA_CYPRESS_ID          0x1080
#define ATA_CYPRESS_82C693      0xc6931080

#define ATA_DEC_21150           0x00221011
#define ATA_DEC_21150_1         0x00231011

#define ATA_HIGHPOINT_ID        0x1103
#define ATA_HPT366              0x00041103
#define ATA_HPT372              0x00051103
#define ATA_HPT302              0x00061103
#define ATA_HPT371              0x00071103
#define ATA_HPT374              0x00081103

#define ATA_INTEL_ID            0x8086
#define ATA_I960RM              0x09628086
#define ATA_I82371FB            0x12308086
#define ATA_I82371SB            0x70108086
#define ATA_I82371AB            0x71118086
#define ATA_I82443MX            0x71998086
#define ATA_I82451NX            0x84ca8086
#define ATA_I82372FB            0x76018086
#define ATA_I82801AB            0x24218086
#define ATA_I82801AA            0x24118086
#define ATA_I82801BA            0x244a8086
#define ATA_I82801BA_1          0x244b8086
#define ATA_I82801CA            0x248a8086
#define ATA_I82801CA_1          0x248b8086
#define ATA_I82801DB            0x24cb8086
#define ATA_I82801DB_1          0x24ca8086
#define ATA_I82801EB            0x24db8086
#define ATA_I82801EB_S1         0x24d18086
#define ATA_I82801EB_R1         0x24df8086
#define ATA_I6300ESB            0x25a28086
#define ATA_I6300ESB_S1         0x25a38086
#define ATA_I6300ESB_R1         0x25b08086
#define ATA_I63XXESB2           0x269e8086
#define ATA_I63XXESB2_S1        0x26808086
#define ATA_I82801FB            0x266f8086
#define ATA_I82801FB_S1         0x26518086
#define ATA_I82801FB_R1         0x26528086
#define ATA_I82801FBM           0x26538086
#define ATA_I82801GB            0x27df8086
#define ATA_I82801GB_S1         0x27c08086
#define ATA_I82801GBM_S1        0x27c48086
#define ATA_I82801HB_S1         0x28208086
#define ATA_I82801HB_S2         0x28258086
#define ATA_I82801HBM           0x28508086
#define ATA_I82801HBM_S1        0x28288086
#define ATA_I82801IB_S1         0x29208086
#define ATA_I82801IB_S3         0x29218086
#define ATA_I82801IB_R1         0x29258086
#define ATA_I82801IB_S2         0x29268086
#define ATA_I82801IBM_S1        0x29288086
#define ATA_I82801IBM_S2        0x292d8086
#define ATA_I82801JIB_S1        0x3a208086
#define ATA_I82801JIB_S2        0x3a268086
#define ATA_I82801JD_S1         0x3a008086
#define ATA_I82801JD_S2         0x3a068086
#define ATA_I82801JI_S1         0x3a208086
#define ATA_I82801JI_S2         0x3a268086

#define ATA_IBP_S1              0x3b208086
#define ATA_IBP_S2              0x3b218086
#define ATA_IBP_S3              0x3b268086
#define ATA_IBP_S4              0x3b288086
#define ATA_IBP_S5              0x3b2d8086
#define ATA_IBP_S6              0x3b2e8086

#define ATA_CPT_S1              0x1c008086
#define ATA_CPT_S2              0x1c018086
#define ATA_CPT_S3              0x1c088086
#define ATA_CPT_S4              0x1c098086

#define ATA_PBG_S1		0x1d008086
#define ATA_PBG_S2		0x1d088086

#define ATA_PPT_S1		0x1e008086
#define ATA_PPT_S2		0x1e018086
#define ATA_PPT_S3		0x1e088086
#define ATA_PPT_S4		0x1e098086

#define ATA_AVOTON_S1		0x1f208086
#define ATA_AVOTON_S2		0x1f218086
#define ATA_AVOTON_S3		0x1f308086
#define ATA_AVOTON_S4		0x1f318086

#define ATA_LPT_S1		0x8c008086
#define ATA_LPT_S2		0x8c018086
#define ATA_LPT_S3		0x8c088086
#define ATA_LPT_S4		0x8c098086

#define ATA_WCPT_S1		0x8c808086
#define ATA_WCPT_S2		0x8c818086
#define ATA_WCPT_S3		0x8c888086
#define ATA_WCPT_S4		0x8c898086

#define ATA_WELLS_S1		0x8d008086
#define ATA_WELLS_S2		0x8d088086
#define ATA_WELLS_S3		0x8d608086
#define ATA_WELLS_S4		0x8d688086

#define ATA_LPTLP_S1		0x9c008086
#define ATA_LPTLP_S2		0x9c018086
#define ATA_LPTLP_S3		0x9c088086
#define ATA_LPTLP_S4		0x9c098086

#define ATA_I31244              0x32008086
#define ATA_ISCH                0x811a8086

#define ATA_COLETOCRK_S1        0x23a18086
#define ATA_COLETOCRK_S2        0x23a68086

#define ATA_ITE_ID              0x1283
#define ATA_IT8211F             0x82111283
#define ATA_IT8212F             0x82121283
#define ATA_IT8213F             0x82131283

#define ATA_JMICRON_ID          0x197b
#define ATA_JMB360              0x2360197b
#define ATA_JMB361              0x2361197b
#define ATA_JMB362              0x2362197b
#define ATA_JMB363              0x2363197b
#define ATA_JMB365              0x2365197b
#define ATA_JMB366              0x2366197b
#define ATA_JMB368              0x2368197b
#define ATA_JMB368_2            0x0368197b

#define ATA_MARVELL_ID          0x11ab
#define ATA_M88SE6101           0x610111ab
#define ATA_M88SE6102           0x610211ab
#define ATA_M88SE6111           0x611111ab
#define ATA_M88SE6121           0x612111ab
#define ATA_M88SE6141           0x614111ab
#define ATA_M88SE6145           0x614511ab
#define ATA_MARVELL2_ID         0x1b4b

#define ATA_MICRON_ID           0x1042
#define ATA_MICRON_RZ1000       0x10001042
#define ATA_MICRON_RZ1001       0x10011042

#define ATA_NATIONAL_ID         0x100b
#define ATA_SC1100              0x0502100b

#define ATA_NETCELL_ID          0x169c
#define ATA_NETCELL_SR          0x0044169c

#define ATA_NVIDIA_ID           0x10de
#define ATA_NFORCE1             0x01bc10de
#define ATA_NFORCE2             0x006510de
#define ATA_NFORCE2_PRO         0x008510de
#define ATA_NFORCE2_PRO_S1      0x008e10de
#define ATA_NFORCE3             0x00d510de
#define ATA_NFORCE3_PRO         0x00e510de
#define ATA_NFORCE3_PRO_S1      0x00e310de
#define ATA_NFORCE3_PRO_S2      0x00ee10de
#define ATA_NFORCE_MCP04        0x003510de
#define ATA_NFORCE_MCP04_S1     0x003610de
#define ATA_NFORCE_MCP04_S2     0x003e10de
#define ATA_NFORCE_CK804        0x005310de
#define ATA_NFORCE_CK804_S1     0x005410de
#define ATA_NFORCE_CK804_S2     0x005510de
#define ATA_NFORCE_MCP51        0x026510de
#define ATA_NFORCE_MCP51_S1     0x026610de
#define ATA_NFORCE_MCP51_S2     0x026710de
#define ATA_NFORCE_MCP55        0x036e10de
#define ATA_NFORCE_MCP55_S1     0x037e10de
#define ATA_NFORCE_MCP55_S2     0x037f10de
#define ATA_NFORCE_MCP61        0x03ec10de
#define ATA_NFORCE_MCP61_S1     0x03e710de
#define ATA_NFORCE_MCP61_S2     0x03f610de
#define ATA_NFORCE_MCP61_S3     0x03f710de
#define ATA_NFORCE_MCP65        0x044810de
#define ATA_NFORCE_MCP65_A0     0x044c10de
#define ATA_NFORCE_MCP65_A1     0x044d10de
#define ATA_NFORCE_MCP65_A2     0x044e10de
#define ATA_NFORCE_MCP65_A3     0x044f10de
#define ATA_NFORCE_MCP65_A4     0x045c10de
#define ATA_NFORCE_MCP65_A5     0x045d10de
#define ATA_NFORCE_MCP65_A6     0x045e10de
#define ATA_NFORCE_MCP65_A7     0x045f10de
#define ATA_NFORCE_MCP67        0x056010de
#define ATA_NFORCE_MCP67_A0     0x055010de
#define ATA_NFORCE_MCP67_A1     0x055110de
#define ATA_NFORCE_MCP67_A2     0x055210de
#define ATA_NFORCE_MCP67_A3     0x055310de
#define ATA_NFORCE_MCP67_A4     0x055410de
#define ATA_NFORCE_MCP67_A5     0x055510de
#define ATA_NFORCE_MCP67_A6     0x055610de
#define ATA_NFORCE_MCP67_A7     0x055710de
#define ATA_NFORCE_MCP67_A8     0x055810de
#define ATA_NFORCE_MCP67_A9     0x055910de
#define ATA_NFORCE_MCP67_AA     0x055A10de
#define ATA_NFORCE_MCP67_AB     0x055B10de
#define ATA_NFORCE_MCP67_AC     0x058410de
#define ATA_NFORCE_MCP73        0x056c10de
#define ATA_NFORCE_MCP73_A0     0x07f010de
#define ATA_NFORCE_MCP73_A1     0x07f110de
#define ATA_NFORCE_MCP73_A2     0x07f210de
#define ATA_NFORCE_MCP73_A3     0x07f310de
#define ATA_NFORCE_MCP73_A4     0x07f410de
#define ATA_NFORCE_MCP73_A5     0x07f510de
#define ATA_NFORCE_MCP73_A6     0x07f610de
#define ATA_NFORCE_MCP73_A7     0x07f710de
#define ATA_NFORCE_MCP73_A8     0x07f810de
#define ATA_NFORCE_MCP73_A9     0x07f910de
#define ATA_NFORCE_MCP73_AA     0x07fa10de
#define ATA_NFORCE_MCP73_AB     0x07fb10de
#define ATA_NFORCE_MCP77        0x075910de
#define ATA_NFORCE_MCP77_A0     0x0ad010de
#define ATA_NFORCE_MCP77_A1     0x0ad110de
#define ATA_NFORCE_MCP77_A2     0x0ad210de
#define ATA_NFORCE_MCP77_A3     0x0ad310de
#define ATA_NFORCE_MCP77_A4     0x0ad410de
#define ATA_NFORCE_MCP77_A5     0x0ad510de
#define ATA_NFORCE_MCP77_A6     0x0ad610de
#define ATA_NFORCE_MCP77_A7     0x0ad710de
#define ATA_NFORCE_MCP77_A8     0x0ad810de
#define ATA_NFORCE_MCP77_A9     0x0ad910de
#define ATA_NFORCE_MCP77_AA     0x0ada10de
#define ATA_NFORCE_MCP77_AB     0x0adb10de
#define ATA_NFORCE_MCP79_A0     0x0ab410de
#define ATA_NFORCE_MCP79_A1     0x0ab510de
#define ATA_NFORCE_MCP79_A2     0x0ab610de
#define ATA_NFORCE_MCP79_A3     0x0ab710de
#define ATA_NFORCE_MCP79_A4     0x0ab810de
#define ATA_NFORCE_MCP79_A5     0x0ab910de
#define ATA_NFORCE_MCP79_A6     0x0aba10de
#define ATA_NFORCE_MCP79_A7     0x0abb10de
#define ATA_NFORCE_MCP79_A8     0x0abc10de
#define ATA_NFORCE_MCP79_A9     0x0abd10de
#define ATA_NFORCE_MCP79_AA     0x0abe10de
#define ATA_NFORCE_MCP79_AB     0x0abf10de
#define ATA_NFORCE_MCP89_A0     0x0d8410de
#define ATA_NFORCE_MCP89_A1     0x0d8510de
#define ATA_NFORCE_MCP89_A2     0x0d8610de
#define ATA_NFORCE_MCP89_A3     0x0d8710de
#define ATA_NFORCE_MCP89_A4     0x0d8810de
#define ATA_NFORCE_MCP89_A5     0x0d8910de
#define ATA_NFORCE_MCP89_A6     0x0d8a10de
#define ATA_NFORCE_MCP89_A7     0x0d8b10de
#define ATA_NFORCE_MCP89_A8     0x0d8c10de
#define ATA_NFORCE_MCP89_A9     0x0d8d10de
#define ATA_NFORCE_MCP89_AA     0x0d8e10de
#define ATA_NFORCE_MCP89_AB     0x0d8f10de

#define ATA_PROMISE_ID          0x105a
#define ATA_PDC20246            0x4d33105a
#define ATA_PDC20262            0x4d38105a
#define ATA_PDC20263            0x0d38105a
#define ATA_PDC20265            0x0d30105a
#define ATA_PDC20267            0x4d30105a
#define ATA_PDC20268            0x4d68105a
#define ATA_PDC20269            0x4d69105a
#define ATA_PDC20270            0x6268105a
#define ATA_PDC20271            0x6269105a
#define ATA_PDC20275            0x1275105a
#define ATA_PDC20276            0x5275105a
#define ATA_PDC20277            0x7275105a
#define ATA_PDC20318            0x3318105a
#define ATA_PDC20319            0x3319105a
#define ATA_PDC20371            0x3371105a
#define ATA_PDC20375            0x3375105a
#define ATA_PDC20376            0x3376105a
#define ATA_PDC20377            0x3377105a
#define ATA_PDC20378            0x3373105a
#define ATA_PDC20379            0x3372105a
#define ATA_PDC20571            0x3571105a
#define ATA_PDC20575            0x3d75105a
#define ATA_PDC20579            0x3574105a
#define ATA_PDC20771            0x3570105a
#define ATA_PDC40518            0x3d18105a
#define ATA_PDC40519            0x3519105a
#define ATA_PDC40718            0x3d17105a
#define ATA_PDC40719            0x3515105a
#define ATA_PDC40775            0x3d73105a
#define ATA_PDC40779            0x3577105a
#define ATA_PDC20617            0x6617105a
#define ATA_PDC20618            0x6626105a
#define ATA_PDC20619            0x6629105a
#define ATA_PDC20620            0x6620105a
#define ATA_PDC20621            0x6621105a
#define ATA_PDC20622            0x6622105a
#define ATA_PDC20624            0x6624105a
#define ATA_PDC81518            0x8002105a

#define ATA_SERVERWORKS_ID      0x1166
#define ATA_ROSB4_ISA           0x02001166
#define ATA_ROSB4               0x02111166
#define ATA_CSB5                0x02121166
#define ATA_CSB6                0x02131166
#define ATA_CSB6_1              0x02171166
#define ATA_HT1000              0x02141166
#define ATA_HT1000_S1           0x024b1166
#define ATA_HT1000_S2           0x024a1166
#define ATA_K2			0x02401166
#define ATA_FRODO4		0x02411166
#define ATA_FRODO8		0x02421166

#define ATA_SILICON_IMAGE_ID    0x1095
#define ATA_SII3114             0x31141095
#define ATA_SII3512             0x35121095
#define ATA_SII3112             0x31121095
#define ATA_SII3112_1           0x02401095
#define ATA_SII0680             0x06801095
#define ATA_CMD646              0x06461095
#define ATA_CMD648              0x06481095
#define ATA_CMD649              0x06491095

#define ATA_SIS_ID              0x1039
#define ATA_SISSOUTH            0x00081039
#define ATA_SIS5511             0x55111039
#define ATA_SIS5513             0x55131039
#define ATA_SIS5517             0x55171039
#define ATA_SIS5518             0x55181039
#define ATA_SIS5571             0x55711039
#define ATA_SIS5591             0x55911039
#define ATA_SIS5596             0x55961039
#define ATA_SIS5597             0x55971039
#define ATA_SIS5598             0x55981039
#define ATA_SIS5600             0x56001039
#define ATA_SIS530              0x05301039
#define ATA_SIS540              0x05401039
#define ATA_SIS550              0x05501039
#define ATA_SIS620              0x06201039
#define ATA_SIS630              0x06301039
#define ATA_SIS635              0x06351039
#define ATA_SIS633              0x06331039
#define ATA_SIS640              0x06401039
#define ATA_SIS645              0x06451039
#define ATA_SIS646              0x06461039
#define ATA_SIS648              0x06481039
#define ATA_SIS650              0x06501039
#define ATA_SIS651              0x06511039
#define ATA_SIS652              0x06521039
#define ATA_SIS655              0x06551039
#define ATA_SIS658              0x06581039
#define ATA_SIS661              0x06611039
#define ATA_SIS730              0x07301039
#define ATA_SIS733              0x07331039
#define ATA_SIS735              0x07351039
#define ATA_SIS740              0x07401039
#define ATA_SIS745              0x07451039
#define ATA_SIS746              0x07461039
#define ATA_SIS748              0x07481039
#define ATA_SIS750              0x07501039
#define ATA_SIS751              0x07511039
#define ATA_SIS752              0x07521039
#define ATA_SIS755              0x07551039
#define ATA_SIS961              0x09611039
#define ATA_SIS962              0x09621039
#define ATA_SIS963              0x09631039
#define ATA_SIS964              0x09641039
#define ATA_SIS965              0x09651039
#define ATA_SIS180              0x01801039
#define ATA_SIS181              0x01811039
#define ATA_SIS182              0x01821039

#define ATA_VIA_ID              0x1106
#define ATA_VIA82C571           0x05711106
#define ATA_VIA82C586           0x05861106
#define ATA_VIA82C596           0x05961106
#define ATA_VIA82C686           0x06861106
#define ATA_VIA8231             0x82311106
#define ATA_VIA8233             0x30741106
#define ATA_VIA8233A            0x31471106
#define ATA_VIA8233C            0x31091106
#define ATA_VIA8235             0x31771106
#define ATA_VIA8237             0x32271106
#define ATA_VIA8237A            0x05911106
#define ATA_VIA8237S		0x53371106
#define ATA_VIA8237_5372	0x53721106
#define ATA_VIA8237_7372	0x73721106
#define ATA_VIA8251             0x33491106
#define ATA_VIA8361             0x31121106
#define ATA_VIA8363             0x03051106
#define ATA_VIA8371             0x03911106
#define ATA_VIA8662             0x31021106
#define ATA_VIA6410             0x31641106
#define ATA_VIA6420             0x31491106
#define ATA_VIA6421             0x32491106
#define ATA_VIACX700IDE         0x05811106
#define ATA_VIACX700            0x83241106
#define ATA_VIASATAIDE          0x53241106
#define ATA_VIAVX800            0x83531106
#define ATA_VIASATAIDE2         0xc4091106
#define ATA_VIAVX855            0x84091106
#define ATA_VIASATAIDE3         0x90011106
#define ATA_VIAVX900            0x84101106

/* global prototypes ata-pci.c */
int ata_pci_probe(device_t dev);
int ata_pci_attach(device_t dev);
int ata_pci_detach(device_t dev);
int ata_pci_suspend(device_t dev);
int ata_pci_resume(device_t dev);
int ata_pci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result);
int ata_pci_write_ivar(device_t dev, device_t child, int which, uintptr_t value);
uint32_t ata_pci_read_config(device_t dev, device_t child, int reg, int width);
void ata_pci_write_config(device_t dev, device_t child, int reg, 
    uint32_t val, int width);
int ata_pci_print_child(device_t dev, device_t child);
int ata_pci_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen);
struct resource * ata_pci_alloc_resource(device_t dev, device_t child, int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);
int ata_pci_release_resource(device_t dev, device_t child, int type, int rid, struct resource *r);
int ata_pci_setup_intr(device_t dev, device_t child, struct resource *irq, int flags, driver_filter_t *filter, driver_intr_t *function, void *argument, void **cookiep);
 int ata_pci_teardown_intr(device_t dev, device_t child, struct resource *irq, void *cookie);
int ata_pci_ch_attach(device_t dev);
int ata_pci_ch_detach(device_t dev);
int ata_pci_status(device_t dev);
void ata_pci_hw(device_t dev);
void ata_pci_dmainit(device_t dev);
void ata_pci_dmafini(device_t dev);
const char *ata_pcivendor2str(device_t dev);
int ata_legacy(device_t);
void ata_generic_intr(void *data);
int ata_generic_chipinit(device_t dev);
int ata_generic_setmode(device_t dev, int target, int mode);
int ata_setup_interrupt(device_t dev, void *intr_func);
void ata_set_desc(device_t dev);
const struct ata_chip_id *ata_match_chip(device_t dev, const struct ata_chip_id *index);
const struct ata_chip_id *ata_find_chip(device_t dev, const struct ata_chip_id *index, int slot);
int ata_mode2idx(int mode);

/* global prototypes from chipsets/ata-*.c */
int ata_sii_chipinit(device_t);

/* externs */
extern devclass_t ata_pci_devclass;

MALLOC_DECLARE(M_ATAPCI);

/* macro for easy definition of all driver module stuff */
#define ATA_DECLARE_DRIVER(dname) \
static device_method_t __CONCAT(dname,_methods)[] = { \
    DEVMETHOD(device_probe,     __CONCAT(dname,_probe)), \
    DEVMETHOD(device_attach,    ata_pci_attach), \
    DEVMETHOD(device_detach,    ata_pci_detach), \
    DEVMETHOD(device_suspend,   ata_pci_suspend), \
    DEVMETHOD(device_resume,    ata_pci_resume), \
    DEVMETHOD(device_shutdown,  bus_generic_shutdown), \
    DEVMETHOD(bus_read_ivar,		ata_pci_read_ivar), \
    DEVMETHOD(bus_write_ivar,		ata_pci_write_ivar), \
    DEVMETHOD(bus_alloc_resource,       ata_pci_alloc_resource), \
    DEVMETHOD(bus_release_resource,     ata_pci_release_resource), \
    DEVMETHOD(bus_activate_resource,    bus_generic_activate_resource), \
    DEVMETHOD(bus_deactivate_resource,  bus_generic_deactivate_resource), \
    DEVMETHOD(bus_setup_intr,           ata_pci_setup_intr), \
    DEVMETHOD(bus_teardown_intr,        ata_pci_teardown_intr), \
    DEVMETHOD(pci_read_config,		ata_pci_read_config), \
    DEVMETHOD(pci_write_config,		ata_pci_write_config), \
    DEVMETHOD(bus_print_child,		ata_pci_print_child), \
    DEVMETHOD(bus_child_location_str,	ata_pci_child_location_str), \
    DEVMETHOD_END \
}; \
static driver_t __CONCAT(dname,_driver) = { \
        "atapci", \
        __CONCAT(dname,_methods), \
        sizeof(struct ata_pci_controller) \
}; \
DRIVER_MODULE(dname, pci, __CONCAT(dname,_driver), ata_pci_devclass, NULL, NULL); \
MODULE_VERSION(dname, 1); \
MODULE_DEPEND(dname, ata, 1, 1, 1); \
MODULE_DEPEND(dname, atapci, 1, 1, 1);
