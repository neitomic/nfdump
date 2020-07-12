/*
 *  Copyright (c) 2009-2020, Peter Haag
 *  Copyright (c) 2004-2008, SWITCH - Teleinformatikdienste fuer Lehre und Forschung
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions are met:
 *  
 *   * Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice, 
 *     this list of conditions and the following disclaimer in the documentation 
 *     and/or other materials provided with the distribution.
 *   * Neither the name of the author nor the names of its contributors may be 
 *     used to endorse or promote products derived from this software without 
 *     specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 *  POSSIBILITY OF SUCH DAMAGE.
 *  
 */

static inline int CheckBufferSpace(nffile_t *nffile, size_t required);

static inline void AppendToBuffer(nffile_t *nffile, void *record, size_t required);

static inline void CopyV6IP(uint32_t *dst, uint32_t *src);

static inline void ExpandRecord_v3(recordHeaderV3_t *v3Record, master_record_t *output_record);

#ifdef NEED_PACKRECORD
static void PackRecordV3(master_record_t *master_record, nffile_t *nffile);
#endif

static inline int CheckBufferSpace(nffile_t *nffile, size_t required) {

	dbg_printf("Buffer Size %u\n", nffile->block_header->size);
	// flush current buffer to disc
	if ( (nffile->block_header->size + required )  > WRITE_BUFFSIZE ) {

		// this should never happen, but catch it anyway
		if ( required > WRITE_BUFFSIZE ) {
			LogError("Required buffer size %zu too big for output buffer!" , required);
			return 0;
		}

		if ( WriteBlock(nffile) <= 0 ) {
			LogError("Failed to write output buffer to disk: '%s'" , strerror(errno));
			return 0;
		} 
	}

	return 1;
} // End of CheckBufferSpace

// Use 4 uint32_t copy cycles, as SPARC CPUs brak
static inline void CopyV6IP(uint32_t *dst, uint32_t *src) {
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
} // End of CopyV6IP

static inline void ExpandRecord_v3(recordHeaderV3_t *v3Record, master_record_t *output_record) {
elementHeader_t *elementHeader;
uint32_t size = sizeof(recordHeaderV3_t);

	void *p   = (void *)v3Record;
	void *eor = p + v3Record->size;

	// set map ref
	output_record->map_ref = NULL;
	output_record->exp_ref = NULL;

	output_record->size = v3Record->size;
	output_record->flags = v3Record->flags;
	output_record->exporter_sysid = v3Record->exporterID;
	output_record->numElements = v3Record->numElements;
	output_record->engine_type = v3Record->engineType;
	output_record->engine_id = v3Record->engineID;

	if ( v3Record->size < size ) {
		LogError("ExpandRecord_v3() Unexpected size: '%u'", v3Record->size);
		abort();
	}

	dbg_printf("Record announces %u extensions with total size %u\n", v3Record->numElements, v3Record->size);
	// first record header
	elementHeader = (elementHeader_t *)(p + sizeof(recordHeaderV3_t));
	for (int i=0; i<v3Record->numElements; i++ ) {
		int skip = 0;
		dbg_printf("[%i] next extension: %u: %s\n", i, elementHeader->type, extensionTable[elementHeader->type].name);
		switch (elementHeader->type) {
			case EXnull:
				fprintf(stderr, "ExpandRecord_v3() Found unexpected NULL extension\n");
				break;
			case EXmsecRelTimeFlowID: {
				EXmsecRelTimeFlow_t *msecRelTimeFlow = (EXmsecRelTimeFlow_t *)((void *)elementHeader + sizeof(elementHeader_t));
				UNUSED(msecRelTimeFlow);
				} break;
			case EXgenericFlowID: {
				EXgenericFlow_t *genericFlow = (EXgenericFlow_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->msecFirst  = genericFlow->msecFirst;
				output_record->msecLast   = genericFlow->msecLast;
				output_record->received	  = genericFlow->msecReceived;
				output_record->dPkts	  = genericFlow->inPackets;
				output_record->dOctets	  = genericFlow->inBytes;
				output_record->srcPort	  = genericFlow->srcPort;
				output_record->dstPort	  = genericFlow->dstPort;
				output_record->proto	  = genericFlow->proto;
				output_record->tcp_flags  = genericFlow->tcpFlags;
				output_record->fwd_status = genericFlow->fwdStatus;
				output_record->tos		  = genericFlow->srcTos;
				} break;
			case EXipv4FlowID: {
				EXipv4Flow_t *ipv4Flow = (EXipv4Flow_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->V6.srcaddr[0] = 0;
				output_record->V6.srcaddr[1] = 0;
				output_record->V4.srcaddr 	 = ipv4Flow->srcAddr;

				output_record->V6.dstaddr[0] = 0;
				output_record->V6.dstaddr[1] = 0;
				output_record->V4.dstaddr 	 = ipv4Flow->dstAddr;
				} break;
			case EXipv6FlowID: {
				EXipv6Flow_t *ipv6Flow = (EXipv6Flow_t *)((void *)elementHeader + sizeof(elementHeader_t));

				output_record->V6.srcaddr[0] = ipv6Flow->srcAddr[0];
				output_record->V6.srcaddr[1] = ipv6Flow->srcAddr[1];
				output_record->V6.dstaddr[0] = ipv6Flow->dstAddr[0];
				output_record->V6.dstaddr[1] = ipv6Flow->dstAddr[1];

				SetFlag(output_record->flags, FLAG_IPV6_ADDR);
				} break;
			case EXflowMiscID: {
				EXflowMisc_t *flowMisc = (EXflowMisc_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->dir		= flowMisc->dir;
				output_record->dst_tos	= flowMisc->dstTos;
				output_record->src_mask	= flowMisc->srcMask;
				output_record->dst_mask	= flowMisc->dstMask;
				output_record->input	= flowMisc->input;
				output_record->output	= flowMisc->output;
				} break;
			case EXcntFlowID: {
				EXcntFlow_t *cntFlow = (EXcntFlow_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->out_pkts	  = cntFlow->outPackets;
				output_record->out_bytes  = cntFlow->outBytes;
				output_record->aggr_flows = cntFlow->flows;
				SetFlag(output_record->flags, FLAG_PKG_64);
				SetFlag(output_record->flags, FLAG_BYTES_64);
				} break;
			case EXvLanID: {
				EXvLan_t *vLan = (EXvLan_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->src_vlan = vLan->srcVlan;
				output_record->dst_vlan = vLan->dstVlan;
				} break;
			case EXasRoutingID: {
				EXasRouting_t *asRouting = (EXasRouting_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->srcas = asRouting->srcAS;
				output_record->dstas = asRouting->dstAS;
				} break;
			case EXbgpNextHopV4ID: {
				EXbgpNextHopV4_t *bgpNextHopV4 = (EXbgpNextHopV4_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->bgp_nexthop.V6[0] = 0;
				output_record->bgp_nexthop.V6[1] = 0;
				output_record->bgp_nexthop.V4	= bgpNextHopV4->ip;
				ClearFlag(output_record->flags, FLAG_IPV6_NHB);
				} break;
			case EXbgpNextHopV6ID: {
				EXbgpNextHopV6_t *bgpNextHopV6 = (EXbgpNextHopV6_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->bgp_nexthop.V6[0] = bgpNextHopV6->ip[0];
				output_record->bgp_nexthop.V6[1] = bgpNextHopV6->ip[1];
				SetFlag(output_record->flags, FLAG_IPV6_NHB);
				} break;
			case EXipNextHopV4ID: {
				EXipNextHopV4_t *ipNextHopV4 = (EXipNextHopV4_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->ip_nexthop.V6[0] = 0;
				output_record->ip_nexthop.V6[1] = 0;
				output_record->ip_nexthop.V4 = ipNextHopV4->ip;
				ClearFlag(output_record->flags, FLAG_IPV6_NH);
				} break;
			case EXipNextHopV6ID: {
				EXipNextHopV6_t *ipNextHopV6 = (EXipNextHopV6_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->ip_nexthop.V6[0] = ipNextHopV6->ip[0];
				output_record->ip_nexthop.V6[1] = ipNextHopV6->ip[1];
				SetFlag(output_record->flags, FLAG_IPV6_NH);
				} break;
			case EXipReceivedV4ID: {
				EXipNextHopV4_t *ipNextHopV4 = (EXipNextHopV4_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->ip_router.V6[0] = 0;
				output_record->ip_router.V6[1] = 0;
				output_record->ip_router.V4 = ipNextHopV4->ip;
				ClearFlag(output_record->flags, FLAG_IPV6_EXP);
				} break;
			case EXipReceivedV6ID: {
				EXipReceivedV6_t *ipNextHopV6 = (EXipReceivedV6_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->ip_router.V6[0] = ipNextHopV6->ip[0];
				output_record->ip_router.V6[1] = ipNextHopV6->ip[1];
				SetFlag(output_record->flags, FLAG_IPV6_EXP);
				} break;
			case EXmplsLabelID: {
				EXmplsLabel_t *mplsLabel = (EXmplsLabel_t *)((void *)elementHeader + sizeof(elementHeader_t));
				for (int j=0; j<10; j++) {
					output_record->mpls_label[j] = mplsLabel->mplsLabel[j];
				}
				} break;
			case EXmacAddrID: {
				EXmacAddr_t *macAddr = (EXmacAddr_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->in_src_mac	= macAddr->inSrcMac;
				output_record->out_dst_mac	= macAddr->outDstMac;
				output_record->in_dst_mac	= macAddr->inDstMac;
				output_record->out_src_mac	= macAddr->outSrcMac;
				} break;
			case EXasAdjacentID: {
				EXasAdjacent_t *asAdjacent = (EXasAdjacent_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->bgpNextAdjacentAS = asAdjacent->nextAdjacentAS;
				output_record->bgpPrevAdjacentAS = asAdjacent->prevAdjacentAS;
				} break;
			case EXlatencyID: {
				EXlatency_t *latency = (EXlatency_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->client_nw_delay_usec = latency->usecClientNwDelay;
				output_record->server_nw_delay_usec = latency->usecServerNwDelay;
				output_record->appl_latency_usec = latency->usecApplLatency;
				} break;
#ifdef NSEL
			case EXnselCommonID: {
				EXnselCommon_t *nselCommon = (EXnselCommon_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->event_flag = FW_EVENT;
				output_record->conn_id 	  = nselCommon->connID;
				output_record->event   	  = nselCommon->fwEvent;
				output_record->fw_xevent  = nselCommon->fwXevent;
				output_record->event_time = nselCommon->msecEvent;
				SetFlag(output_record->flags, FLAG_EVENT);
			} break;
			case EXnselXlateIPv4ID: {
				EXnselXlateIPv4_t *nselXlateIPv4 = (EXnselXlateIPv4_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->xlate_src_ip.V6[0] = 0;
				output_record->xlate_src_ip.V6[1] = 0;
				output_record->xlate_src_ip.V4	= nselXlateIPv4->xlateSrcAddr;
				output_record->xlate_dst_ip.V6[0] = 0;
				output_record->xlate_dst_ip.V6[1] = 0;
				output_record->xlate_dst_ip.V4	= nselXlateIPv4->xlateDstAddr;
				output_record->xlate_flags = 0;
			} break;
			case EXnselXlateIPv6ID: {
				EXnselXlateIPv6_t *nselXlateIPv6 = (EXnselXlateIPv6_t *)((void *)elementHeader + sizeof(elementHeader_t));
				memcpy(output_record->xlate_src_ip.V6, &(nselXlateIPv6->xlateSrcAddr), 16);
				memcpy(output_record->xlate_dst_ip.V6, &(nselXlateIPv6->xlateDstAddr), 16);
				output_record->xlate_flags = 1;
			} break;
			case EXnselXlatePortID: {
				EXnselXlatePort_t *nselXlatePort = (EXnselXlatePort_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->xlate_src_port = nselXlatePort->xlateSrcPort;
				output_record->xlate_dst_port = nselXlatePort->xlateDstPort;
			} break;
			case EXnselAclID: {
				EXnselAcl_t *nselAcl = (EXnselAcl_t *)((void *)elementHeader + sizeof(elementHeader_t));
				memcpy(output_record->ingress_acl_id, nselAcl->ingressAcl, 12);
				memcpy(output_record->egress_acl_id, nselAcl->egressAcl, 12);
			} break;
			case EXnselUserID: {
				EXnselUser_t *nselUser = (EXnselUser_t *)((void *)elementHeader + sizeof(elementHeader_t));
				memcpy(output_record->username, nselUser->username, 66);
			} break;
			case EXnelCommonID: {
				EXnelCommon_t *nelCommon = (EXnelCommon_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->event_time = nelCommon->msecEvent;
				output_record->event 	  = nelCommon->natEvent;
				output_record->event_flag = FW_EVENT;
				output_record->egress_vrfid  = nelCommon->egressVrf;
				output_record->ingress_vrfid = nelCommon->ingressVrf;
			} break;
			case EXnelXlatePortID: {
				EXnelXlatePort_t *nelXlatePort = (EXnelXlatePort_t *)((void *)elementHeader + sizeof(elementHeader_t));
				output_record->block_start = nelXlatePort->blockStart;
				output_record->block_end   = nelXlatePort->blockEnd;
				output_record->block_step  = nelXlatePort->blockStep;
				output_record->block_size  = nelXlatePort->blockSize;
			} break;
#endif
			default:
				fprintf(stderr, "Unknown extension '%u'\n", elementHeader->type);
				skip = 1;
		}

		if (!skip) {
			// unordered element list
			// output_record->exElementList[i] = elementHeader->type;

			// insert element in order to list
			int j = 0;
			uint32_t val = elementHeader->type;
			while ( j < i ) {
				if ( val < output_record->exElementList[j] ) {
					uint32_t _tmp = output_record->exElementList[j];
					output_record->exElementList[j] = val;
					val = _tmp;
				}
				j++;
			}
			output_record->exElementList[j] = val;

		} else {
			skip = 0;
		}

		size += elementHeader->length;
		elementHeader = (elementHeader_t *)((void *)elementHeader + elementHeader->length);

		if( (void *)elementHeader > eor ) {
			fprintf(stderr, "ptr error - elementHeader > eor\n");
			exit(255);
		}
	}
	// map icmp type/code in it's own vars
	output_record->icmp = output_record->dstPort;
	if ( size != v3Record->size ) {
		LogError("Record size info: '%u' not equal sum extensions: '%u'", v3Record->size, size);
		exit(255);
	}

	// at least one flow
	if ( output_record->aggr_flows == 0 )
		output_record->aggr_flows = 1;

	if ( output_record->numElements > MAXELEMENTS ) {
		LogError("Number of elements %u exceeds max number defined %u" , output_record->numElements, MAXELEMENTS);
		exit(255);
	}
} // End of ExpandRecord_v3


#ifdef NEED_PACKRECORD
static void PackRecordV3(master_record_t *master_record, nffile_t *nffile) {
uint32_t required;

	required = master_record->size;

	// flush current buffer to disc if not enough space
	if ( !CheckBufferSpace(nffile, required) ) {
		return;
	}

	// enough buffer space available at this point
	AddV3Header(nffile->buff_ptr, v3Record);
	v3Record->flags		  = master_record->flags;
	v3Record->engineType  = master_record->engine_type;
	v3Record->engineID	  = master_record->engine_id;

	// first record header
	for (int i=0; i<master_record->numElements; i++ ) {
		dbg_printf("Pack extension %u\n", master_record->exElementList[i]);
		switch (master_record->exElementList[i]) {
			case EXnull:
				fprintf(stderr, "PackRecordV3(): Found unexpected NULL extension\n");
				break;
			case EXgenericFlowID: {
				PushExtension(v3Record, EXgenericFlow, genericFlow);
				genericFlow->msecFirst = master_record->msecFirst;
				genericFlow->msecLast  = master_record->msecLast;
				genericFlow->msecReceived = master_record->received;
				genericFlow->inPackets	= master_record->dPkts;
				genericFlow->inBytes	= master_record->dOctets;
 				genericFlow->srcPort	= master_record->srcPort;
 				genericFlow->dstPort	= master_record->dstPort;
				genericFlow->proto		= master_record->proto;
				genericFlow->tcpFlags	= master_record->tcp_flags;
				genericFlow->fwdStatus	 = master_record->fwd_status;
				genericFlow->srcTos 	 = master_record->tos;
				} break;
			case EXipv4FlowID: {
				PushExtension(v3Record, EXipv4Flow, ipv4Flow);
				ipv4Flow->srcAddr	= master_record->V4.srcaddr;
				ipv4Flow->dstAddr	= master_record->V4.dstaddr;
				} break;
			case EXipv6FlowID: {
				PushExtension(v3Record, EXipv6Flow, ipv6Flow);
				ipv6Flow->srcAddr[0] = master_record->V6.srcaddr[0];
				ipv6Flow->srcAddr[1] = master_record->V6.srcaddr[1];
				ipv6Flow->dstAddr[0] = master_record->V6.dstaddr[0];
				ipv6Flow->dstAddr[1] = master_record->V6.dstaddr[1];
				} break;
			case EXflowMiscID: {
				PushExtension(v3Record, EXflowMisc, flowMisc);
				flowMisc->input   = master_record->input;
				flowMisc->output  = master_record->output;
 				flowMisc->dir	  = master_record->dir;
				flowMisc->dstTos  = master_record->dst_tos;
				flowMisc->srcMask = master_record->src_mask;
				flowMisc->dstMask = master_record->dst_mask;
				} break;
			case EXcntFlowID: {
				PushExtension(v3Record, EXcntFlow, cntFlow);
				cntFlow->outPackets	= master_record->out_pkts;
				cntFlow->outBytes	= master_record->out_bytes;
				cntFlow->flows		= master_record->aggr_flows;
				} break;
			case EXvLanID: {
				PushExtension(v3Record, EXvLan, vLan);
				vLan->srcVlan	= master_record->src_vlan;
				vLan->dstVlan	= master_record->dst_vlan;
				} break;
			case EXasRoutingID: {
				PushExtension(v3Record, EXasRouting, asRouting);
				asRouting->srcAS	= master_record->srcas;
				asRouting->dstAS	= master_record->dstas;
				} break;
			case EXbgpNextHopV4ID: {
				PushExtension(v3Record, EXbgpNextHopV4, bgpNextHopV4);
				bgpNextHopV4->ip = master_record->bgp_nexthop.V4;
				} break;
			case EXbgpNextHopV6ID: {
				PushExtension(v3Record, EXbgpNextHopV6, bgpNextHopV6);
				bgpNextHopV6->ip[0] = master_record->bgp_nexthop.V6[0];
				bgpNextHopV6->ip[1] = master_record->bgp_nexthop.V6[1];
				} break;
			case EXipNextHopV4ID: {
				PushExtension(v3Record, EXipNextHopV4, ipNextHopV4);
				ipNextHopV4->ip	= master_record->ip_nexthop.V4;
				} break;
			case EXipNextHopV6ID: {
				PushExtension(v3Record, EXipNextHopV6, ipNextHopV6);
				ipNextHopV6->ip[0] = master_record->ip_nexthop.V6[0];
				ipNextHopV6->ip[1] = master_record->ip_nexthop.V6[1];
				} break;
			case EXipReceivedV4ID: {
				PushExtension(v3Record, EXipReceivedV4, ipNextHopV4);
				ipNextHopV4->ip = master_record->ip_router.V4;
				} break;
			case EXipReceivedV6ID: {
				PushExtension(v3Record, EXipReceivedV6, ipNextHopV6);
				ipNextHopV6->ip[0] = master_record->ip_router.V6[0];
				ipNextHopV6->ip[1] = master_record->ip_router.V6[1];
				} break;
			case EXmplsLabelID: {
				PushExtension(v3Record, EXmplsLabel, mplsLabel);
				for (int j=0; j<10; j++) {
					mplsLabel->mplsLabel[j] = master_record->mpls_label[j];
				}
				} break;
			case EXmacAddrID: {
				PushExtension(v3Record, EXmacAddr, macAddr);
				macAddr->inSrcMac	= master_record->in_src_mac;
				macAddr->outDstMac	= master_record->out_dst_mac;
				macAddr->inDstMac	= master_record->in_dst_mac;
				macAddr->outSrcMac	= master_record->out_src_mac;
				} break;
			case EXasAdjacentID: {
				PushExtension(v3Record, EXasAdjacent, asAdjacent);
				asAdjacent->nextAdjacentAS = master_record->bgpNextAdjacentAS;
				asAdjacent->prevAdjacentAS = master_record->bgpPrevAdjacentAS;
				} break;
			case EXlatencyID: {
				PushExtension(v3Record, EXlatency, latency);
				latency->usecClientNwDelay = master_record->client_nw_delay_usec;
				latency->usecServerNwDelay = master_record->server_nw_delay_usec;
				latency->usecApplLatency   = master_record->appl_latency_usec;
				} break;
#ifdef NSEL
			case EXnselCommonID: {
				PushExtension(v3Record, EXnselCommon, nselCommon);
				nselCommon->msecEvent = master_record->event_time;
				nselCommon->connID    = master_record->conn_id;
				nselCommon->fwXevent  = master_record->fw_xevent;
				nselCommon->fwEvent   = master_record->event;
				} break;
			case EXnselXlateIPv4ID: {
				PushExtension(v3Record, EXnselXlateIPv4, nselXlateIPv4);
				nselXlateIPv4->xlateSrcAddr = master_record->xlate_src_ip.V4;
				nselXlateIPv4->xlateDstAddr = master_record->xlate_dst_ip.V4;
				} break;
			case EXnselXlateIPv6ID: {
				PushExtension(v3Record, EXnselXlateIPv6, nselXlateIPv6);
				memcpy(nselXlateIPv6->xlateSrcAddr, master_record->xlate_src_ip.V6, 16);
				memcpy(nselXlateIPv6->xlateDstAddr, master_record->xlate_dst_ip.V6, 16);
				} break;
			case EXnselXlatePortID: {
				PushExtension(v3Record, EXnselXlatePort, nselXlatePort);
				nselXlatePort->xlateSrcPort = master_record->xlate_src_port;
				nselXlatePort->xlateDstPort = master_record->xlate_dst_port;
				} break;
			case EXnselAclID: {
				PushExtension(v3Record, EXnselAcl, nselAcl);
				memcpy(nselAcl->ingressAcl, master_record->ingress_acl_id, 12);
				memcpy(nselAcl->egressAcl, master_record->egress_acl_id, 12);
				} break;
			case EXnselUserID: {
				PushExtension(v3Record, EXnselUser, nselUser);
				memcpy(nselUser->username, master_record->username, 65);
				nselUser->username[65] = '\0';
				} break;
			case EXnelCommonID: {
				PushExtension(v3Record, EXnelCommon, nelCommon);
				nelCommon->msecEvent  = master_record->event_time;
				nelCommon->natEvent   = master_record->event;
				nelCommon->egressVrf  = master_record->egress_vrfid;
				nelCommon->ingressVrf = master_record->ingress_vrfid;
				} break;
			case EXnelXlatePortID: {
				PushExtension(v3Record, EXnelXlatePort, nelXlatePort);
				nelXlatePort->blockStart = master_record->block_start;
				nelXlatePort->blockEnd   = master_record->block_end;
				nelXlatePort->blockStep  = master_record->block_step;
				nelXlatePort->blockSize  = master_record->block_size;
				} break;
#endif
			default:
				fprintf(stderr, "PackRecordV3(): Unknown extension '%u'\n", master_record->exElementList[i]);
		}
		if ( v3Record->size > required ) {
			fprintf(stderr, "PackRecordV3(): record size(%u) > expected(%u)'\n", v3Record->size, required);
		}
	}

	if ( v3Record->size != required ) {
		fprintf(stderr, "PackRecordV3(): record size(%u) != expected(%u)'\n", v3Record->size, required);
	}
	nffile->block_header->size 	+= v3Record->size;
	nffile->block_header->NumRecords++;
	dbg_assert( v3Record->size == required );
	nffile->buff_ptr += v3Record->size;

} // End of PackRecordV3
#endif

static inline void AppendToBuffer(nffile_t *nffile, void *record, size_t required) {

	// flush current buffer to disc
	if ( !CheckBufferSpace(nffile, required)) {
		return;
	}

	// enough buffer space available at this point
	memcpy(nffile->buff_ptr, record, required);

	// update stat
	nffile->block_header->NumRecords++;
	nffile->block_header->size += required;

	// advance write pointer
	nffile->buff_ptr = (void *)((pointer_addr_t)nffile->buff_ptr + required);

} // End of AppendToBuffer
