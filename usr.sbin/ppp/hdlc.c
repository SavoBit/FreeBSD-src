/*
 *	     PPP High Level Link Control (HDLC) Module
 *
 *	    Written by Toshiharu OHNO (tony-o@iij.ad.jp)
 *
 *   Copyright (C) 1993, Internet Initiative Japan, Inc. All rights reserverd.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the Internet Initiative Japan, Inc.  The name of the
 * IIJ may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * $Id: hdlc.c,v 1.28.2.29 1998/04/24 19:16:03 brian Exp $
 *
 *	TODO:
 */
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcpproto.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "ipcp.h"
#include "ip.h"
#include "vjcomp.h"
#include "auth.h"
#include "pap.h"
#include "chap.h"
#include "lcp.h"
#include "async.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"
#include "prompt.h"
#include "chat.h"
#include "mp.h"
#include "datalink.h"
#include "filter.h"
#include "bundle.h"

static u_short const fcstab[256] = {
   /* 00 */ 0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
   /* 08 */ 0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
   /* 10 */ 0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
   /* 18 */ 0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
   /* 20 */ 0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
   /* 28 */ 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
   /* 30 */ 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
   /* 38 */ 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
   /* 40 */ 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
   /* 48 */ 0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
   /* 50 */ 0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
   /* 58 */ 0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
   /* 60 */ 0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
   /* 68 */ 0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
   /* 70 */ 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
   /* 78 */ 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
   /* 80 */ 0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
   /* 88 */ 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
   /* 90 */ 0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
   /* 98 */ 0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
   /* a0 */ 0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
   /* a8 */ 0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
   /* b0 */ 0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
   /* b8 */ 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
   /* c0 */ 0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
   /* c8 */ 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
   /* d0 */ 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
   /* d8 */ 0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
   /* e0 */ 0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
   /* e8 */ 0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
   /* f0 */ 0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
   /* f8 */ 0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

void
hdlc_Init(struct hdlc *hdlc, struct lcp *lcp)
{
  memset(hdlc, '\0', sizeof(struct hdlc));
  hdlc->lqm.owner = lcp;
}

/*
 *  HDLC FCS computation. Read RFC 1171 Appendix B and CCITT X.25 section
 *  2.27 for further details.
 */
inline u_short
HdlcFcs(u_short fcs, u_char * cp, int len)
{
  while (len--)
    fcs = (fcs >> 8) ^ fcstab[(fcs ^ *cp++) & 0xff];
  return (fcs);
}

static inline u_short
HdlcFcsBuf(u_short fcs, struct mbuf *m)
{
  int len;
  u_char *pos, *end;

  len = plength(m);
  pos = MBUF_CTOP(m);
  end = pos + m->cnt;
  while (len--) {
    fcs = (fcs >> 8) ^ fcstab[(fcs ^ *pos++) & 0xff];
    if (pos == end && len) {
      m = m->next;
      pos = MBUF_CTOP(m);
      end = pos + m->cnt;
    }
  }
  return (fcs);
}

void
HdlcOutput(struct link *l, int pri, u_short proto, struct mbuf *bp)
{
  struct physical *p = link2physical(l);
  struct mbuf *mhp, *mfcs;
  u_char *cp;
  u_short fcs;

  if (!p || Physical_IsSync(p))
    mfcs = NULL;
  else
    mfcs = mballoc(2, MB_HDLCOUT);

  mhp = mballoc(4, MB_HDLCOUT);
  mhp->cnt = 0;
  cp = MBUF_CTOP(mhp);
  if (p && (proto == PROTO_LCP || l->lcp.his_acfcomp == 0)) {
    *cp++ = HDLC_ADDR;
    *cp++ = HDLC_UI;
    mhp->cnt += 2;
  }

  /*
   * If possible, compress protocol field.
   */
  if (l->lcp.his_protocomp && (proto & 0xff00) == 0) {
    *cp++ = proto;
    mhp->cnt++;
  } else {
    *cp++ = proto >> 8;
    *cp = proto & 0377;
    mhp->cnt += 2;
  }

  mhp->next = bp;

  if (!p) {
    /*
     * This is where we multiplex the data over our available physical
     * links.  We don't frame our logical link data.  Instead we wait
     * for the logical link implementation to chop our data up and pile
     * it into the physical links by re-calling this function with the
     * encapsulated fragments.
     */
    link_Output(l, pri, mhp);
    return;
  }

  /* Tack mfcs onto the end, then set bp back to the start of the data */
  while (bp->next != NULL)
    bp = bp->next;
  bp->next = mfcs;
  bp = mhp->next;

  p->hdlc.lqm.OutOctets += plength(mhp) + 1;
  p->hdlc.lqm.OutPackets++;

  if (proto == PROTO_LQR) {
    /* Overwrite the entire packet */
    struct lqrdata lqr;

    lqr.MagicNumber = p->link.lcp.want_magic;
    lqr.LastOutLQRs = p->hdlc.lqm.lqr.peer.PeerOutLQRs;
    lqr.LastOutPackets = p->hdlc.lqm.lqr.peer.PeerOutPackets;
    lqr.LastOutOctets = p->hdlc.lqm.lqr.peer.PeerOutOctets;
    lqr.PeerInLQRs = p->hdlc.lqm.lqr.SaveInLQRs;
    lqr.PeerInPackets = p->hdlc.lqm.SaveInPackets;
    lqr.PeerInDiscards = p->hdlc.lqm.SaveInDiscards;
    lqr.PeerInErrors = p->hdlc.lqm.SaveInErrors;
    lqr.PeerInOctets = p->hdlc.lqm.SaveInOctets;
    lqr.PeerOutPackets = p->hdlc.lqm.OutPackets;
    lqr.PeerOutOctets = p->hdlc.lqm.OutOctets;
    if (p->hdlc.lqm.lqr.peer.LastOutLQRs == p->hdlc.lqm.lqr.OutLQRs) {
      /*
       * only increment if it's the first time or we've got a reply
       * from the last one
       */
      lqr.PeerOutLQRs = ++p->hdlc.lqm.lqr.OutLQRs;
      LqrDump(l->name, "Output", &lqr);
    } else {
      lqr.PeerOutLQRs = p->hdlc.lqm.lqr.OutLQRs;
      LqrDump(l->name, "Output (again)", &lqr);
    }
    LqrChangeOrder(&lqr, (struct lqrdata *)MBUF_CTOP(bp));
  }

  if (mfcs) {
    mfcs->cnt = 0;
    fcs = HdlcFcsBuf(INITFCS, mhp);
    fcs = ~fcs;
    cp = MBUF_CTOP(mfcs);
    *cp++ = fcs & 0377;		/* Low byte first!! */
    *cp++ = fcs >> 8;
    mfcs->cnt = 2;
  }

  LogDumpBp(LogHDLC, "HdlcOutput", mhp);

  link_ProtocolRecord(l, proto, PROTO_OUT);
  LogPrintf(LogDEBUG, "HdlcOutput: proto = 0x%04x\n", proto);

  if (Physical_IsSync(p))
    link_Output(l, pri, mhp);          /* Send it raw */
  else
    async_Output(pri, mhp, proto, p);
}

/* Check out the latest ``Assigned numbers'' rfc (rfc1700.txt) */
static struct {
  u_short from;
  u_short to;
  const char *name;
} protocols[] = {
  { 0x0001, 0x0001, "Padding Protocol" },
  { 0x0003, 0x001f, "reserved (transparency inefficient)" },
  { 0x0021, 0x0021, "Internet Protocol" },
  { 0x0023, 0x0023, "OSI Network Layer" },
  { 0x0025, 0x0025, "Xerox NS IDP" },
  { 0x0027, 0x0027, "DECnet Phase IV" },
  { 0x0029, 0x0029, "Appletalk" },
  { 0x002b, 0x002b, "Novell IPX" },
  { 0x002d, 0x002d, "Van Jacobson Compressed TCP/IP" },
  { 0x002f, 0x002f, "Van Jacobson Uncompressed TCP/IP" },
  { 0x0031, 0x0031, "Bridging PDU" },
  { 0x0033, 0x0033, "Stream Protocol (ST-II)" },
  { 0x0035, 0x0035, "Banyan Vines" },
  { 0x0037, 0x0037, "reserved (until 1993)" },
  { 0x0039, 0x0039, "AppleTalk EDDP" },
  { 0x003b, 0x003b, "AppleTalk SmartBuffered" },
  { 0x003d, 0x003d, "Multi-Link" },
  { 0x003f, 0x003f, "NETBIOS Framing" },
  { 0x0041, 0x0041, "Cisco Systems" },
  { 0x0043, 0x0043, "Ascom Timeplex" },
  { 0x0045, 0x0045, "Fujitsu Link Backup and Load Balancing (LBLB)" },
  { 0x0047, 0x0047, "DCA Remote Lan" },
  { 0x0049, 0x0049, "Serial Data Transport Protocol (PPP-SDTP)" },
  { 0x004b, 0x004b, "SNA over 802.2" },
  { 0x004d, 0x004d, "SNA" },
  { 0x004f, 0x004f, "IP6 Header Compression" },
  { 0x0051, 0x0051, "KNX Bridging Data" },
  { 0x0053, 0x0053, "Encryption" },
  { 0x0055, 0x0055, "Individual Link Encryption" },
  { 0x006f, 0x006f, "Stampede Bridging" },
  { 0x0071, 0x0071, "BAP Bandwidth Allocation Protocol" },
  { 0x0073, 0x0073, "MP+ Protocol" },
  { 0x007d, 0x007d, "reserved (Control Escape)" },
  { 0x007f, 0x007f, "reserved (compression inefficient)" },
  { 0x00cf, 0x00cf, "reserved (PPP NLPID)" },
  { 0x00fb, 0x00fb, "compression on single link in multilink group" },
  { 0x00fd, 0x00fd, "1st choice compression" },
  { 0x00ff, 0x00ff, "reserved (compression inefficient)" },
  { 0x0200, 0x02ff, "(compression inefficient)" },
  { 0x0201, 0x0201, "802.1d Hello Packets" },
  { 0x0203, 0x0203, "IBM Source Routing BPDU" },
  { 0x0205, 0x0205, "DEC LANBridge100 Spanning Tree" },
  { 0x0207, 0x0207, "Cisco Discovery Protocol" },
  { 0x0209, 0x0209, "Netcs Twin Routing" },
  { 0x0231, 0x0231, "Luxcom" },
  { 0x0233, 0x0233, "Sigma Network Systems" },
  { 0x0235, 0x0235, "Apple Client Server Protocol" },
  { 0x1e00, 0x1eff, "(compression inefficient)" },
  { 0x4001, 0x4001, "Cray Communications Control Protocol" },
  { 0x4003, 0x4003, "CDPD Mobile Network Registration Protocol" },
  { 0x4021, 0x4021, "Stacker LZS" },
  { 0x8001, 0x801f, "Not Used - reserved" },
  { 0x8021, 0x8021, "Internet Protocol Control Protocol" },
  { 0x8023, 0x8023, "OSI Network Layer Control Protocol" },
  { 0x8025, 0x8025, "Xerox NS IDP Control Protocol" },
  { 0x8027, 0x8027, "DECnet Phase IV Control Protocol" },
  { 0x8029, 0x8029, "Appletalk Control Protocol" },
  { 0x802b, 0x802b, "Novell IPX Control Protocol" },
  { 0x802d, 0x802d, "reserved" },
  { 0x802f, 0x802f, "reserved" },
  { 0x8031, 0x8031, "Bridging NCP" },
  { 0x8033, 0x8033, "Stream Protocol Control Protocol" },
  { 0x8035, 0x8035, "Banyan Vines Control Protocol" },
  { 0x8037, 0x8037, "reserved till 1993" },
  { 0x8039, 0x8039, "reserved" },
  { 0x803b, 0x803b, "reserved" },
  { 0x803d, 0x803d, "Multi-Link Control Protocol" },
  { 0x803f, 0x803f, "NETBIOS Framing Control Protocol" },
  { 0x8041, 0x8041, "Cisco Systems Control Protocol" },
  { 0x8043, 0x8043, "Ascom Timeplex" },
  { 0x8045, 0x8045, "Fujitsu LBLB Control Protocol" },
  { 0x8047, 0x8047, "DCA Remote Lan Network Control Protocol (RLNCP)" },
  { 0x8049, 0x8049, "Serial Data Control Protocol (PPP-SDCP)" },
  { 0x804b, 0x804b, "SNA over 802.2 Control Protocol" },
  { 0x804d, 0x804d, "SNA Control Protocol" },
  { 0x804f, 0x804f, "IP6 Header Compression Control Protocol" },
  { 0x8051, 0x8051, "KNX Bridging Control Protocol" },
  { 0x8053, 0x8053, "Encryption Control Protocol" },
  { 0x8055, 0x8055, "Individual Link Encryption Control Protocol" },
  { 0x806f, 0x806f, "Stampede Bridging Control Protocol" },
  { 0x8073, 0x8073, "MP+ Control Protocol" },
  { 0x8071, 0x8071, "BACP Bandwidth Allocation Control Protocol" },
  { 0x807d, 0x807d, "Not Used - reserved" },
  { 0x80cf, 0x80cf, "Not Used - reserved" },
  { 0x80fb, 0x80fb, "compression on single link in multilink group control" },
  { 0x80fd, 0x80fd, "Compression Control Protocol" },
  { 0x80ff, 0x80ff, "Not Used - reserved" },
  { 0x8207, 0x8207, "Cisco Discovery Protocol Control" },
  { 0x8209, 0x8209, "Netcs Twin Routing" },
  { 0x8235, 0x8235, "Apple Client Server Protocol Control" },
  { 0xc021, 0xc021, "Link Control Protocol" },
  { 0xc023, 0xc023, "Password Authentication Protocol" },
  { 0xc025, 0xc025, "Link Quality Report" },
  { 0xc027, 0xc027, "Shiva Password Authentication Protocol" },
  { 0xc029, 0xc029, "CallBack Control Protocol (CBCP)" },
  { 0xc081, 0xc081, "Container Control Protocol" },
  { 0xc223, 0xc223, "Challenge Handshake Authentication Protocol" },
  { 0xc225, 0xc225, "RSA Authentication Protocol" },
  { 0xc227, 0xc227, "Extensible Authentication Protocol" },
  { 0xc26f, 0xc26f, "Stampede Bridging Authorization Protocol" },
  { 0xc281, 0xc281, "Proprietary Authentication Protocol" },
  { 0xc283, 0xc283, "Proprietary Authentication Protocol" },
  { 0xc481, 0xc481, "Proprietary Node ID Authentication Protocol" }
};

#define NPROTOCOLS (sizeof protocols/sizeof protocols[0])

const char *
hdlc_Protocol2Nam(u_short proto)
{
  int f;

  for (f = 0; f < NPROTOCOLS; f++)
    if (proto >= protocols[f].from && proto <= protocols[f].to)
      return protocols[f].name;
    else if (proto < protocols[f].from)
      break;
  return "unrecognised protocol";
}

void
hdlc_DecodePacket(struct bundle *bundle, u_short proto, struct mbuf * bp,
                  struct link *l)
{
  struct physical *p = link2physical(l);
  u_char *cp;

  LogPrintf(LogDEBUG, "DecodePacket: proto = 0x%04x\n", proto);

  /* decompress everything.  CCP needs uncompressed data too */
  if ((bp = ccp_Decompress(&l->ccp, &proto, bp)) == NULL)
    return;

  switch (proto) {
  case PROTO_LCP:
    LcpInput(&l->lcp, bp);
    break;
  case PROTO_PAP:
    if (p)
      PapInput(bundle, bp, p);
    else {
      LogPrintf(LogERROR, "DecodePacket: PAP: Not a physical link !\n");
      pfree(bp);
    }
    break;
  case PROTO_LQR:
    if (p) {
      p->hdlc.lqm.lqr.SaveInLQRs++;
      LqrInput(p, bp);
    } else {
      LogPrintf(LogERROR, "DecodePacket: LQR: Not a physical link !\n");
      pfree(bp);
    }
    break;
  case PROTO_CHAP:
    if (p)
      ChapInput(bundle, bp, p);
    else {
      LogPrintf(LogERROR, "DecodePacket: CHAP: Not a physical link !\n");
      pfree(bp);
    }
    break;
  case PROTO_VJUNCOMP:
  case PROTO_VJCOMP:
    bp = VjCompInput(&bundle->ncp.ipcp, bp, proto);
    if (bp == NULL)
      break;
    /* fall down */
  case PROTO_IP:
    IpInput(bundle, bp);
    break;
  case PROTO_IPCP:
    IpcpInput(&bundle->ncp.ipcp, bp);
    break;
  case PROTO_CCP:
    CcpInput(&l->ccp, bundle, bp);
    break;
  case PROTO_MP:
    if (bundle->ncp.mp.active) {
      if (p)
        mp_Input(&bundle->ncp.mp, bp, p);
      else {
        LogPrintf(LogERROR, "DecodePacket: MP inside MP ?!\n");
        pfree(bp);
      }
      break;
    }
    /* Fall through */
  default:
    LogPrintf(LogPHASE, "%s protocol 0x%04x (%s)\n",
              proto == PROTO_MP ? "Unexpected" : "Unknown",
              proto, hdlc_Protocol2Nam(proto));
    bp->offset -= 2;
    bp->cnt += 2;
    cp = MBUF_CTOP(bp);
    lcp_SendProtoRej(&l->lcp, cp, bp->cnt);
    if (p) {
      p->hdlc.lqm.SaveInDiscards++;
      p->hdlc.stats.unknownproto++;
    }
    pfree(bp);
    break;
  }
}

void
HdlcInput(struct bundle *bundle, struct mbuf * bp, struct physical *physical)
{
  u_short fcs, proto;
  u_char *cp, addr, ctrl;

  LogDumpBp(LogHDLC, "HdlcInput:", bp);
  if (Physical_IsSync(physical))
    fcs = GOODFCS;
  else
    fcs = HdlcFcs(INITFCS, MBUF_CTOP(bp), bp->cnt);
  physical->hdlc.lqm.SaveInOctets += bp->cnt + 1;

  LogPrintf(LogDEBUG, "HdlcInput: fcs = %04x (%s)\n",
	    fcs, (fcs == GOODFCS) ? "good" : "bad");
  if (fcs != GOODFCS) {
    physical->hdlc.lqm.SaveInErrors++;
    LogPrintf(LogDEBUG, "HdlcInput: Bad FCS\n");
    physical->hdlc.stats.badfcs++;
    pfree(bp);
    return;
  }
  if (!Physical_IsSync(physical))
    bp->cnt -= 2;		/* discard FCS part */

  if (bp->cnt < 2) {		/* XXX: raise this bar ? */
    pfree(bp);
    return;
  }
  cp = MBUF_CTOP(bp);

  if (!physical->link.lcp.want_acfcomp) {
    /*
     * We expect that packet is not compressed.
     */
    addr = *cp++;
    if (addr != HDLC_ADDR) {
      physical->hdlc.lqm.SaveInErrors++;
      physical->hdlc.stats.badaddr++;
      LogPrintf(LogDEBUG, "HdlcInput: addr %02x\n", *cp);
      pfree(bp);
      return;
    }
    ctrl = *cp++;
    if (ctrl != HDLC_UI) {
      physical->hdlc.lqm.SaveInErrors++;
      physical->hdlc.stats.badcommand++;
      LogPrintf(LogDEBUG, "HdlcInput: %02x\n", *cp);
      pfree(bp);
      return;
    }
    bp->offset += 2;
    bp->cnt -= 2;
  } else if (cp[0] == HDLC_ADDR && cp[1] == HDLC_UI) {

    /*
     * We can receive compressed packet, but peer still send uncompressed
     * packet to me.
     */
    cp += 2;
    bp->offset += 2;
    bp->cnt -= 2;
  }
  if (physical->link.lcp.want_protocomp) {
    proto = 0;
    cp--;
    do {
      cp++;
      bp->offset++;
      bp->cnt--;
      proto = proto << 8;
      proto += *cp;
    } while (!(proto & 1));
  } else {
    proto = *cp++ << 8;
    proto |= *cp++;
    bp->offset += 2;
    bp->cnt -= 2;
  }

  link_ProtocolRecord(physical2link(physical), proto, PROTO_IN);
  physical->hdlc.lqm.SaveInPackets++;

  hdlc_DecodePacket(bundle, proto, bp, physical2link(physical));
}

/*
 *  Detect a HDLC frame
 */

static const char *FrameHeaders[] = {
  "\176\377\003\300\041",
  "\176\377\175\043\300\041",
  "\176\177\175\043\100\041",
  "\176\175\337\175\043\300\041",
  "\176\175\137\175\043\100\041",
  NULL,
};

u_char *
HdlcDetect(struct physical *physical, u_char *cp, int n)
{
  const char *fp, **hp;
  char *ptr;

  cp[n] = '\0';			/* be sure to null terminated */
  ptr = NULL;
  for (hp = FrameHeaders; *hp; hp++) {
    fp = *hp;
    if (Physical_IsSync(physical))
      fp++;
    ptr = strstr((char *) cp, fp);
    if (ptr)
      break;
  }
  return (u_char *)ptr;
}

int
hdlc_ReportStatus(struct cmdargs const *arg)
{
  struct hdlc *hdlc = &arg->cx->physical->hdlc;

  prompt_Printf(arg->prompt, "HDLC level errors:\n");
  prompt_Printf(arg->prompt, " Bad Frame Check Sequence fields: %u\n",
	        hdlc->stats.badfcs);
  prompt_Printf(arg->prompt, " Bad address (!= 0x%02x) fields:    %u\n",
	        HDLC_ADDR, hdlc->stats.badaddr);
  prompt_Printf(arg->prompt, " Bad command (!= 0x%02x) fields:    %u\n",
	        HDLC_UI, hdlc->stats.badcommand);
  prompt_Printf(arg->prompt, " Unrecognised protocol fields:    %u\n",
	        hdlc->stats.unknownproto);
  return 0;
}

static void
hdlc_ReportTime(void *v)
{
  /* Moan about HDLC errors */
  struct hdlc *hdlc = (struct hdlc *)v;

  StopTimer(&hdlc->ReportTimer);

  if (memcmp(&hdlc->laststats, &hdlc->stats, sizeof hdlc->stats)) {
    LogPrintf(LogPHASE,
              "HDLC errors -> FCS: %u, ADDR: %u, COMD: %u, PROTO: %u\n",
	      hdlc->stats.badfcs - hdlc->laststats.badfcs,
              hdlc->stats.badaddr - hdlc->laststats.badaddr,
              hdlc->stats.badcommand - hdlc->laststats.badcommand,
              hdlc->stats.unknownproto - hdlc->laststats.unknownproto);
    hdlc->laststats = hdlc->stats;
  }

  StartTimer(&hdlc->ReportTimer);
}

void
hdlc_StartTimer(struct hdlc *hdlc)
{
  StopTimer(&hdlc->ReportTimer);
  hdlc->ReportTimer.load = 60 * SECTICKS;
  hdlc->ReportTimer.arg = hdlc;
  hdlc->ReportTimer.func = hdlc_ReportTime;
  hdlc->ReportTimer.name = "hdlc";
  StartTimer(&hdlc->ReportTimer);
}

void
hdlc_StopTimer(struct hdlc *hdlc)
{
  StopTimer(&hdlc->ReportTimer);
}
