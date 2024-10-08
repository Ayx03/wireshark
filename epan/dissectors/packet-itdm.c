/* packet-itdm.c
 * Routines for I-TDM (Internal TDM) dissection
 * Compliant to PICMG SFP.0 and SFP.1 March 24, 2005
 *
 * Copyright 2008, Dan Gora <dg [AT] adax.com>
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/prefs.h>
#include <epan/tfs.h>
#include <wsutil/array.h>

void proto_register_itdm(void);
void proto_reg_handoff_itdm(void);

/* Initialize the protocol and registered fields */
static int proto_itdm;
static int hf_itdm_timestamp;
static int hf_itdm_seqnum;
static int hf_itdm_sop_eop;
static int hf_itdm_last_pack;
static int hf_itdm_pktlen;
static int hf_itdm_chksum;
static int hf_itdm_uid;
static int hf_itdm_ack;
static int hf_itdm_act;
static int hf_itdm_chcmd;
static int hf_itdm_chid;
static int hf_itdm_chloc1;
static int hf_itdm_chloc2;
static int hf_itdm_pktrate;
static int hf_itdm_cxnsize;

/* I-TDM control protocol fields */
static int hf_itdm_ctl_transid;
static int hf_itdm_ctl_command;
static int hf_itdm_ctl_flowid;
static int hf_itdm_ctl_dm;
static int hf_itdm_ctl_emts;
static int hf_itdm_ctl_pktrate;
static int hf_itdm_ctl_ptid;
static int hf_itdm_ctl_cksum;


/* Initialize the subtree pointers */
static int ett_itdm;
static int ett_itdm_ctl;

static dissector_handle_t itdm_handle;

/* ZZZZ some magic number.. */
static unsigned gbl_ItdmMPLSLabel = 0x99887;
static unsigned gbl_ItdmCTLFlowNo;

/* I-TDM 125usec mode commands for data flows */
#define ITDM_CMD_NEW_CHAN     1
#define ITDM_CMD_CLOSE_CHAN   2
#define ITDM_CMD_RELOC_CHAN   3
#define ITDM_CMD_CYCLIC_REAF  4
#define ITDM_CMD_PACKET_RATE  5

#define ITDM_FLOWID_OFFSET    7
#define ITDM_CHCMD_OFFSET    10
#define ITDM_CHANID_OFFSET   11
#define ITDM_CHLOC1_OFFSET   14
#define ITDM_CHLOC2_OFFSET   16

/* I-TDM commands for I-TDM control flows */
#define ITDM_CTL_TRANSID_OFFSET    10
#define ITDM_CTL_CMD_OFFSET        14
#define ITDM_CTL_FLOWID_OFFSET     15
#define ITDM_CTL_ITDM_MODE_OFFSET  18
#define ITDM_CTL_EMTS_OFFSET       20
#define ITDM_CTL_PKTRATE_OFFSET    22
#define ITDM_CTL_PAIRED_TRANSID_OFFSET    26
#define ITDM_CTL_CRC_OFFSET        30

#define ITDM_CTL_CMD_AFI_REQ  1

static const value_string sop_eop_vals[] = {
  { 0x0, "Middle of Packet" },
  { 0x1, "End of Packet" },
  { 0x2, "Start of Packet" },
  { 0x3, "Complete Packet" },
  { 0, NULL }
};

static const true_false_string ack_tfs = {
  "Acknowledging a command from remote node",
  "Normal Command"
};

static const value_string chcmd_vals[] = {
  { 0x0, "Reserved" },
  { 0x1, "New Channel ID" },
  { 0x2, "Close Channel ID" },
  { 0x3, "Relocate Channel ID" },
  { 0x4, "Cyclic Reaffirmation" },
  { 0x5, "Packet Rate Integrity Check" },
  { 0x6, "Reserved" },
  { 0x7, "Reserved" },
  { 0x8, "Reserved" },
  { 0x9, "Reserved" },
  { 0xa, "Reserved" },
  { 0xb, "Reserved" },
  { 0xc, "Reserved" },
  { 0xd, "Reserved" },
  { 0xe, "Reserved" },
  { 0xf, "Reserved" },
  { 0, NULL }
};

static const value_string itdm_ctl_command_vals[] = {
  { 0x0, "Not Used" },
  { 0x1, "AFI_REQ: Alloc Flow ID Req" },
  { 0x2, "AFI_RSP: Alloc Flow ID Rsp - Req Accepted." },
  { 0x3, "DFI_REQ: Dealloc Flow ID Req" },
  { 0x4, "DFI_RSP: Dealloc Flow ID Rsp - Req Accepted." },

  { 0x10, "AFI_RSP: Reject: Data Mode Field value Not Supported." },
  { 0x11, "AFI_RSP: Reject: Explicit Multi-timeslot value Not Supported." },
  { 0x12, "AFI_RSP: Reject: Packet Rate value Not Supported." },
  { 0x13, "AFI_RSP: Reject: Checksum Invalid." },
  { 0x14, "AFI_RSP: Reject: No more flows available." },

  { 0x20, "DFI_RSP: Reject: Data Mode Field value does not match Flow ID." },
  { 0x21, "DFI_RSP: Reject: Explicit Multi-timeslots value does not match." },
  { 0x22, "DFI_RSP: Reject: Packet Rate value does not match." },
  { 0x23, "DFI_RSP: Reject: Checksum Invalid." },
  { 0x24, "DFI_RSP: Reject: Flow ID invalid (out of range)." },
  { 0x25, "DFI_RSP: Reject: Flow ID not currently allocated." },
  { 0x26, "DFI_RSP: Reject: Other Flow ID in pair has active connections." },
  { 0, NULL }
};

static const value_string itdm_ctl_data_mode_vals[] = {
  { 0, "Not Used." },
  { 1, "I-TDM 1ms Data Mode." },
  { 2, "I-TDM 125usec Data Mode." },
  { 3, "I-TDM Explicit Multi-timeslot Data Mode." },
  { 4, "I-TDM CAS Signaling Data Mode." },
  { 0, NULL }
};

static const value_string itdm_ctl_pktrate_vals[] = {
  { 0x447A0000, "I-TDM 1ms Data Mode." },
  { 0x45FA0000, "I-TDM 125usec/EMTS Data Mode." },
  { 0x43A6AAAB, "I-TDM T1 CAS Mode." },
  { 0x43FA0000, "I-TDM E1 CAS Mode." },
  { 0, NULL }
};

static void
dissect_itdm_125usec(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  tvbuff_t  *next_tvb;
  proto_item *itdm_item = NULL;
  proto_tree *itdm_tree = NULL;
  int offset;
  uint32_t flowid;
  uint32_t chanid;
  uint16_t chloc1;
  uint16_t chloc2;
  uint8_t chcmd;
  uint8_t actbit;
  uint8_t ackbit;


  col_set_str(pinfo->cinfo, COL_PROTOCOL, "ITDM");

  flowid = tvb_get_ntoh24(tvb, ITDM_FLOWID_OFFSET);
  chanid = tvb_get_ntoh24(tvb, ITDM_CHANID_OFFSET);
  chcmd  = tvb_get_uint8(tvb, ITDM_CHCMD_OFFSET);
  chloc1 = tvb_get_ntohs(tvb, ITDM_CHLOC1_OFFSET);
  actbit = (chcmd & 0x10) ? 1 : 0;
  ackbit = (chcmd & 0x20) ? 1 : 0;
  chcmd  = chcmd & 0x0f;

  col_add_fstr(pinfo->cinfo, COL_INFO,
      "Flow %d Chan %d ACT %d ACK %d %s",
      flowid, chanid, actbit, ackbit,
      val_to_str_const(chcmd, chcmd_vals, "Reserved"));
  if (chcmd == ITDM_CMD_NEW_CHAN ||
      chcmd == ITDM_CMD_CLOSE_CHAN ||
      chcmd == ITDM_CMD_CYCLIC_REAF)
  {
    col_append_fstr(pinfo->cinfo, COL_INFO,
        " Loc1 %d", chloc1);
  }
  else if (chcmd == ITDM_CMD_RELOC_CHAN)
  {
    chloc2 = tvb_get_ntohs(tvb, ITDM_CHLOC2_OFFSET);
    col_append_fstr(pinfo->cinfo, COL_INFO,
      " Loc1 %d Loc2 %d", chloc1, chloc2);
  }

  offset = 0;

  if (tree)
  {
  itdm_item = proto_tree_add_item(tree, proto_itdm, tvb, 0, -1, ENC_NA);
  itdm_tree = proto_item_add_subtree(itdm_item, ett_itdm);

  proto_tree_add_item(itdm_tree, hf_itdm_timestamp, tvb, offset, 2, ENC_BIG_ENDIAN);
  offset += 2;
  proto_tree_add_item(itdm_tree, hf_itdm_seqnum, tvb, offset, 1, ENC_BIG_ENDIAN);
  offset += 1;
  proto_tree_add_item(itdm_tree, hf_itdm_sop_eop, tvb, offset, 1, ENC_BIG_ENDIAN);
  proto_tree_add_item(itdm_tree, hf_itdm_last_pack, tvb, offset, 1, ENC_BIG_ENDIAN);
  proto_tree_add_item(itdm_tree, hf_itdm_pktlen, tvb, offset, 2, ENC_BIG_ENDIAN);
  offset += 2;
  proto_tree_add_checksum(itdm_tree, tvb, offset, hf_itdm_chksum, -1, NULL, pinfo, 0, ENC_BIG_ENDIAN, PROTO_CHECKSUM_NO_FLAGS);
  offset += 2;
  proto_tree_add_item(itdm_tree, hf_itdm_uid, tvb, offset, 3, ENC_BIG_ENDIAN);
  offset += 3;
  proto_tree_add_item(itdm_tree, hf_itdm_ack, tvb, offset, 1, ENC_BIG_ENDIAN);
  proto_tree_add_item(itdm_tree, hf_itdm_act, tvb, offset, 1, ENC_BIG_ENDIAN);
  proto_tree_add_item(itdm_tree, hf_itdm_chcmd, tvb, offset, 1, ENC_BIG_ENDIAN);
  offset += 1;
  proto_tree_add_item(itdm_tree, hf_itdm_chid, tvb, offset, 3, ENC_BIG_ENDIAN);
  offset += 3;
  if (chcmd == ITDM_CMD_PACKET_RATE)
  {
    proto_tree_add_item(itdm_tree, hf_itdm_pktrate, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;
  }
  else
  {
    proto_tree_add_item(itdm_tree, hf_itdm_chloc1, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;
    if (chcmd == ITDM_CMD_CYCLIC_REAF ||
        chcmd == ITDM_CMD_NEW_CHAN ||
        chcmd == ITDM_CMD_CLOSE_CHAN)
    {
      proto_tree_add_item(itdm_tree, hf_itdm_cxnsize, tvb, offset, 2, ENC_BIG_ENDIAN);
      offset += 2;
    }
    else
    {
      proto_tree_add_item(itdm_tree, hf_itdm_chloc2, tvb, offset, 2, ENC_BIG_ENDIAN);
      offset += 2;
    }
  }
  }

  next_tvb = tvb_new_subset_remaining(tvb, offset);
  call_data_dissector(next_tvb, pinfo, tree);
}

static void
dissect_itdm_control(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  tvbuff_t  *next_tvb;
  proto_item *itdm_ctl_item = NULL;
  proto_tree *itdm_ctl_tree = NULL;
  int offset;
  uint32_t flowid;
  uint8_t command;
  uint32_t trans_id;
  uint32_t paired_trans_id;
  uint32_t allocd_flowid;

  col_set_str(pinfo->cinfo, COL_PROTOCOL, "ITDM-Control");

  flowid = tvb_get_ntoh24(tvb, ITDM_FLOWID_OFFSET);
  command = tvb_get_uint8(tvb, ITDM_CTL_CMD_OFFSET);
  allocd_flowid = tvb_get_ntoh24(tvb, ITDM_CTL_FLOWID_OFFSET);
  trans_id = tvb_get_ntohl(tvb, ITDM_CTL_TRANSID_OFFSET);
  paired_trans_id = tvb_get_ntohl(tvb, ITDM_CTL_PAIRED_TRANSID_OFFSET);

  col_add_fstr(pinfo->cinfo, COL_INFO,
      "Flow %d Command %s ",
      flowid, val_to_str_const(command, itdm_ctl_command_vals, "Reserved"));

  if (command != ITDM_CTL_CMD_AFI_REQ )
  {
    col_append_fstr(pinfo->cinfo, COL_INFO,
        " Alloc'd FlowID %d", allocd_flowid);
  }

  col_append_fstr(pinfo->cinfo, COL_INFO, " TransID 0x%x ", trans_id);

  if (command != ITDM_CTL_CMD_AFI_REQ )
  {
    col_append_fstr(pinfo->cinfo, COL_INFO,
        " Paired TransID 0x%x", paired_trans_id);
  }

  offset = 0;

  if (tree)
  {
  itdm_ctl_item = proto_tree_add_item(tree, proto_itdm, tvb, 0, -1, ENC_NA);
  itdm_ctl_tree = proto_item_add_subtree(itdm_ctl_item, ett_itdm_ctl);

  /* These eventually should go into a SFP.0 dissector... */
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_timestamp, tvb, offset, 2, ENC_BIG_ENDIAN);
  offset += 2;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_seqnum, tvb, offset, 1, ENC_BIG_ENDIAN);
  offset += 1;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_sop_eop, tvb, offset, 1, ENC_BIG_ENDIAN);
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_last_pack, tvb, offset, 1, ENC_BIG_ENDIAN);
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_pktlen, tvb, offset, 2, ENC_BIG_ENDIAN);
  offset += 2;
  proto_tree_add_checksum(itdm_ctl_tree, tvb, offset, hf_itdm_chksum, -1, NULL, pinfo, 0, ENC_BIG_ENDIAN, PROTO_CHECKSUM_NO_FLAGS);
  offset += 2;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_uid, tvb, offset, 3, ENC_BIG_ENDIAN);
  offset += 3;

  proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_transid, tvb, offset, 4, ENC_BIG_ENDIAN);
  offset += 4;

  proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_command, tvb, offset, 1, ENC_BIG_ENDIAN);
  offset += 1;
  if (command != ITDM_CTL_CMD_AFI_REQ) {
    proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_flowid, tvb, offset, 3, ENC_BIG_ENDIAN);
  }
  offset += 3;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_dm, tvb, offset, 1, ENC_BIG_ENDIAN);
  offset += 1;
  /* rsvd.. */
  offset += 1;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_emts, tvb, offset, 2, ENC_BIG_ENDIAN);
  offset += 2;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_pktrate, tvb, offset, 4, ENC_BIG_ENDIAN);
  offset += 4;
  if (command != ITDM_CTL_CMD_AFI_REQ) {
    proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_ptid, tvb, offset, 4, ENC_BIG_ENDIAN);
  }
  offset += 4;
  /* rsvd.. */
  offset += 2;
  proto_tree_add_item(itdm_ctl_tree, hf_itdm_ctl_cksum, tvb, offset, 2, ENC_BIG_ENDIAN);
  offset += 2;
  }

  next_tvb = tvb_new_subset_remaining(tvb, offset);
  call_data_dissector(next_tvb, pinfo, tree);
}

static int
dissect_itdm(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_)
{
  uint32_t flowid;

  /* ZZZ for now, 125 usec mode and I-TDM control protocol
   * need to add 1ms mode */
  if (tvb_captured_length(tvb) < 18)
    return 0;

  /* See if this packet is a data flow or the I-TDM control flow. */
  flowid = tvb_get_ntoh24(tvb, ITDM_FLOWID_OFFSET);

  /* gbl_ItdmCTLFlowNo is the configurable flow number where
   * the control protocol resides... Usually 0.
   */
  if (flowid == gbl_ItdmCTLFlowNo)
    dissect_itdm_control(tvb, pinfo, tree);
  else
    dissect_itdm_125usec(tvb, pinfo, tree);
  return tvb_captured_length(tvb);
}

void
proto_register_itdm(void)
{

  static hf_register_info hf[] = {
    { &hf_itdm_timestamp,{ "Timestamp", "itdm.timestamp",
      FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_seqnum,{ "Sequence Number", "itdm.seqnum",
      FT_UINT8, BASE_DEC, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_sop_eop,{ "Start/End of Packet", "itdm.sop_eop",
      FT_UINT8, BASE_DEC, VALS(sop_eop_vals), 0xc0, NULL, HFILL } },
    { &hf_itdm_last_pack,{ "Last Packet", "itdm.last_pack",
      FT_BOOLEAN, 8, NULL, 0x20, NULL, HFILL } },
    { &hf_itdm_pktlen,{ "Packet Length", "itdm.pktlen",
      FT_UINT16, BASE_DEC, NULL, 0x07ff, NULL, HFILL } },
    { &hf_itdm_chksum,{ "Checksum", "itdm.chksum",
      FT_UINT16, BASE_HEX, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_uid,{ "Flow ID", "itdm.uid",
      FT_UINT24, BASE_DEC, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_ack,{ "ACK", "itdm.ack",
      FT_BOOLEAN, 8, TFS(&ack_tfs), 0x20, NULL, HFILL } },
    { &hf_itdm_act,{ "Activate", "itdm.act",
      FT_BOOLEAN, 8, NULL, 0x10, NULL, HFILL } },
    { &hf_itdm_chcmd,{ "Channel Command", "itdm.chcmd",
      FT_UINT8, BASE_DEC, VALS(chcmd_vals), 0x0f, NULL, HFILL } },
    { &hf_itdm_chid,{ "Channel ID", "itdm.chid",
      FT_UINT24, BASE_DEC, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_chloc1,{ "Channel Location 1", "itdm.chloc1",
      FT_UINT16, BASE_DEC, NULL, 0x01ff, NULL, HFILL } },
    { &hf_itdm_chloc2,{ "Channel Location 2", "itdm.chloc2",
      FT_UINT16, BASE_DEC, NULL, 0x01ff, NULL, HFILL } },
    { &hf_itdm_pktrate,{ "IEEE 754 Packet Rate", "itdm.pktrate",
       FT_UINT32, BASE_HEX, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_cxnsize, { "Connection Size", "itdm.cxnsize",
       FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL } },

    { &hf_itdm_ctl_transid, { "Transaction ID", "itdm.ctl_transid",
       FT_UINT32, BASE_HEX, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_command, { "Control Command", "itdm.ctl_cmd",
       FT_UINT8, BASE_DEC, VALS(itdm_ctl_command_vals), 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_flowid, { "Allocated Flow ID", "itdm.ctl_flowid",
       FT_UINT24, BASE_DEC, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_dm, { "I-TDM Data Mode", "itdm.ctl_dm",
       FT_UINT8, BASE_DEC, VALS(itdm_ctl_data_mode_vals), 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_emts, { "I-TDM Explicit Multi-timeslot Size", "itdm.ctlemts",
       FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_pktrate, { "I-TDM Packet Rate", "itdm.ctl_pktrate",
       FT_UINT32, BASE_HEX, VALS(itdm_ctl_pktrate_vals), 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_ptid, { "Paired Transaction ID", "itdm.ctl_ptid",
       FT_UINT32, BASE_HEX, NULL, 0x0, NULL, HFILL } },
    { &hf_itdm_ctl_cksum, { "ITDM Control Message Checksum", "itdm.ctl_cksum",
       FT_UINT16, BASE_HEX, NULL, 0x0, NULL, HFILL } }
  };

  static int *ett[] = {
    &ett_itdm,
    &ett_itdm_ctl
  };

  module_t *itdm_module;

  proto_itdm = proto_register_protocol("Internal TDM", "ITDM", "itdm");
  itdm_handle = register_dissector("itdm", dissect_itdm, proto_itdm);

  proto_register_field_array(proto_itdm, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));

  itdm_module = prefs_register_protocol(proto_itdm, proto_reg_handoff_itdm);

  prefs_register_uint_preference(itdm_module, "mpls_label",
    "ITDM MPLS label (Flow Bundle ID in hex)",
    "The MPLS label (aka Flow Bundle ID) used by ITDM traffic.",
    16, &gbl_ItdmMPLSLabel);

  prefs_register_uint_preference(itdm_module, "ctl_flowno",
    "I-TDM Control Protocol Flow Number",
    "Flow Number used by I-TDM Control Protocol traffic.",
    10, &gbl_ItdmCTLFlowNo);
}

void
proto_reg_handoff_itdm(void)
{
  static bool Initialized=false;
  static unsigned ItdmMPLSLabel;

  if (!Initialized) {
    Initialized=true;
  } else {
    dissector_delete_uint("mpls.label", ItdmMPLSLabel, itdm_handle);
  }

  ItdmMPLSLabel = gbl_ItdmMPLSLabel;
  dissector_add_uint("mpls.label", gbl_ItdmMPLSLabel, itdm_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
