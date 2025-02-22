/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "Common.h"
#include "IPU.h"
#include "IPU/IPUdma.h"
#include "mpeg2lib/Mpeg.h"

#include "Vif.h"
#include "Gif.h"
#include "Vif_Dma.h"

static IPUStatus IPU1Status;
static tIPU_DMA g_nDMATransfer;

void ipuDmaReset()
{
	IPU1Status.InProgress	= false;
	IPU1Status.DMAMode		= DMA_MODE_NORMAL;
	IPU1Status.DMAFinished	= true;

	g_nDMATransfer.reset();
}

void SaveStateBase::ipuDmaFreeze()
{
	FreezeTag( "IPUdma" );
	Freeze(g_nDMATransfer);
	Freeze(IPU1Status);
}

static __fi void ipuDmacSrcChain()
{
	switch (IPU1Status.ChainMode)
	{
		case TAG_REFE: // refe
			//if(!IPU1Status.InProgress) ipu1ch.tadr += 16;
			IPU1Status.DMAFinished = true;
			break;
		case TAG_CNT: // cnt
			// Set the taddr to the next tag
			ipu1ch.tadr = ipu1ch.madr;
			//if(!IPU1Status.DMAFinished) IPU1Status.DMAFinished = false;
			break;

		case TAG_NEXT: // next
			ipu1ch.tadr = IPU1Status.NextMem;
			//if(!IPU1Status.DMAFinished) IPU1Status.DMAFinished = false;
			break;

		case TAG_REF: // ref
			//if(!IPU1Status.InProgress)ipu1ch.tadr += 16;
			//if(!IPU1Status.DMAFinished) IPU1Status.DMAFinished = false;
			break;

		case TAG_END: // end
			//ipu1ch.tadr = ipu1ch.madr;
			//IPU1Status.DMAFinished = true;
			break;
	}
}

static __fi int IPU1chain() {

	int totalqwc = 0;

	if (ipu1ch.qwc > 0 && IPU1Status.InProgress)
	{
		int qwc = ipu1ch.qwc;
		u32 *pMem;

		pMem = (u32*)dmaGetAddr(ipu1ch.madr, false);

		if (pMem == NULL)
		{
			log_cb(RETRO_LOG_ERROR, "ipu1dma NULL!\n");
			return totalqwc;
		}

		//Write our data to the fifo
		qwc = ipu_fifo.in.write(pMem, qwc);
		ipu1ch.madr += qwc << 4;
		ipu1ch.qwc -= qwc;
		totalqwc += qwc;
	}

	//Update TADR etc
	
	hwDmacSrcTadrInc(ipu1ch); 
	
	if( ipu1ch.qwc == 0)
		IPU1Status.InProgress = false;

	return totalqwc;
}

int IPU1dma()
{
	int ipu1cycles = 0;
	int totalqwc = 0;

	//We need to make sure GIF has flushed before sending IPU data, it seems to REALLY screw FFX videos

	if(!ipu1ch.chcr.STR || IPU1Status.DMAMode == DMA_MODE_INTERLEAVE)
	{
		//We MUST stop the IPU from trying to fill the FIFO with more data if the DMA has been suspended
		//if we don't, we risk causing the data to go out of sync with the fifo and we end up losing some!
		//This is true for Dragons Quest 8 and probably others which suspend the DMA.
#ifndef NDEBUG
		log_cb(RETRO_LOG_DEBUG, "IPU1 running when IPU1 DMA disabled! CHCR %x QWC %x\n", ipu1ch.chcr._u32, ipu1ch.qwc);
#endif
		return 0;
	}

	IPU_LOG("IPU1 DMA Called QWC %x Finished %d In Progress %d tadr %x", ipu1ch.qwc, IPU1Status.DMAFinished, IPU1Status.InProgress, ipu1ch.tadr);

	switch(IPU1Status.DMAMode)
	{
		case DMA_MODE_NORMAL:
			{
				IPU_LOG("Processing Normal QWC left %x Finished %d In Progress %d", ipu1ch.qwc, IPU1Status.DMAFinished, IPU1Status.InProgress);
				if(IPU1Status.InProgress) totalqwc += IPU1chain();
			}
			break;

		case DMA_MODE_CHAIN:
			{
				if(IPU1Status.InProgress) //No transfer is ready to go so we need to set one up
				{
					IPU_LOG("Processing Chain QWC left %x Finished %d In Progress %d", ipu1ch.qwc, IPU1Status.DMAFinished, IPU1Status.InProgress);
					totalqwc += IPU1chain();
				}


				if(!IPU1Status.InProgress && !IPU1Status.DMAFinished) //No transfer is ready to go so we need to set one up
				{
					tDMA_TAG* ptag = dmaGetAddr(ipu1ch.tadr, false);  //Set memory pointer to TADR

					if (!ipu1ch.transfer("IPU1", ptag))
					{
						return totalqwc;
					}
					ipu1ch.madr = ptag[1]._u32;

					ipu1cycles += 1; // Add 1 cycles from the QW read for the tag
					IPU1Status.ChainMode = ptag->ID;

#ifndef NDEBUG
					if(ipu1ch.chcr.TTE)
						log_cb(RETRO_LOG_DEBUG, "TTE?\n");
#endif
					
					IPU1Status.DMAFinished = hwDmacSrcChain(ipu1ch, ptag->ID);

					
					if(ipu1ch.qwc > 0) IPU1Status.InProgress = true;
					IPU_LOG("dmaIPU1 dmaChain %8.8x_%8.8x size=%d, addr=%lx, fifosize=%x",
							ptag[1]._u32, ptag[0]._u32, ipu1ch.qwc, ipu1ch.madr, 8 - g_BP.IFC);

					if (ipu1ch.chcr.TIE && ptag->IRQ) //Tag Interrupt is set, so schedule the end/interrupt
						IPU1Status.DMAFinished = true;

					IPU_LOG("Processing Start Chain QWC left %x Finished %d In Progress %d", ipu1ch.qwc, IPU1Status.DMAFinished, IPU1Status.InProgress);
					totalqwc += IPU1chain();
					//Set the TADR forward
				}

			}
			break;
	}

	//Do this here to prevent double settings on Chain DMA's
	if(totalqwc > 0 || ipu1ch.qwc == 0)
	{
		IPU_INT_TO(totalqwc * BIAS);
		IPUProcessInterrupt();
	}
	else 
	{
		cpuRegs.eCycle[4] = 0x9999;//IPU_INT_TO(2048);
	}

	IPU_LOG("Completed Call IPU1 DMA QWC Remaining %x Finished %d In Progress %d tadr %x", ipu1ch.qwc, IPU1Status.DMAFinished, IPU1Status.InProgress, ipu1ch.tadr);
	return totalqwc;
}

void IPU0dma()
{
	if(!ipuRegs.ctrl.OFC) 
	{
		IPUProcessInterrupt();
		return;
	}

	int readsize;
	tDMA_TAG* pMem;

	if ((!(ipu0ch.chcr.STR) || (cpuRegs.interrupt & (1 << DMAC_FROM_IPU))) || (ipu0ch.qwc == 0))
	{
#ifndef NDEBUG
		log_cb(RETRO_LOG_DEBUG, "How??\n");
#endif
		return;
	}

	pxAssert(!(ipu0ch.chcr.TTE));

	IPU_LOG("dmaIPU0 chcr = %lx, madr = %lx, qwc  = %lx",
	        ipu0ch.chcr._u32, ipu0ch.madr, ipu0ch.qwc);

	pxAssert(ipu0ch.chcr.MOD == NORMAL_MODE);

	pMem = dmaGetAddr(ipu0ch.madr, true);

	readsize = std::min(ipu0ch.qwc, (u32)ipuRegs.ctrl.OFC);
	ipu_fifo.out.read(pMem, readsize);

	ipu0ch.madr += readsize << 4;
	ipu0ch.qwc -= readsize;
	
	if (dmacRegs.ctrl.STS == STS_fromIPU && ipu0ch.qwc == 0)   // STS == fromIPU
	{
		//log_cb(RETRO_LOG_WARN, "fromIPU Stall Control\n");
		dmacRegs.stadr.ADDR = ipu0ch.madr;
		switch (dmacRegs.ctrl.STD)
		{
			case NO_STD:
				break;
			case STD_GIF: // GIF
				//log_cb(RETRO_LOG_WARN, "GIFSTALL\n");
				g_nDMATransfer.GIFSTALL = true;
				break;
			case STD_VIF1: // VIF
				//log_cb(RETRO_LOG_WARN, "VIFSTALL\n");
				g_nDMATransfer.VIFSTALL = true;
				break;
			case STD_SIF1:
				//	log_cb(RETRO_LOG_WARN, "SIFSTALL\n");
				g_nDMATransfer.SIFSTALL = true;
				break;
		}
	}
	//Fixme ( voodoocycles ):
	//This was IPU_INT_FROM(readsize*BIAS );
	//This broke vids in Digital Devil Saga
	//Note that interrupting based on totalsize is just guessing..
	
	IPU_INT_FROM( readsize * BIAS );
	if (ipuRegs.ctrl.IFC > 0) { IPUProcessInterrupt(); }

	//return readsize;
}

__fi void dmaIPU0() // fromIPU
{
#ifndef NDEBUG
	if (dmacRegs.ctrl.STS == STS_fromIPU)
		log_cb(RETRO_LOG_WARN, "DMA Stall enabled on IPU0");
#endif

	if (dmacRegs.ctrl.STS == STS_fromIPU)   // STS == fromIPU - Initial settings
		dmacRegs.stadr.ADDR = ipu0ch.madr;

	// Note: This should probably be a very small value, however anything lower than this will break Mana Khemia
	// This is because the game sends bad DMA information, starts an IDEC, then sets it to the correct values
	// but because our IPU is too quick, it messes up the sync between the DMA and IPU.
	// So this will do until (if) we sort the timing out of IPU, shouldn't cause any problems for games for now.
	IPU_INT_FROM(160);


	
}

__fi void dmaIPU1() // toIPU
{
	IPU_LOG("IPU1DMAStart QWC %x, MADR %x, CHCR %x, TADR %x", ipu1ch.qwc, ipu1ch.madr, ipu1ch.chcr._u32, ipu1ch.tadr);

	if (ipu1ch.chcr.MOD == CHAIN_MODE)  //Chain Mode
	{
		IPU_LOG("Setting up IPU1 Chain mode");
		if(ipu1ch.qwc == 0)
		{
			IPU1Status.InProgress = false;
			IPU1Status.DMAFinished = false;
		}
		else
		{   //Attempting to continue a previous chain
			IPU_LOG("Resuming DMA TAG %x", (ipu1ch.chcr.TAG >> 12));
			//We MUST check the CHCR for the tag it last knew, it can be manipulated!
			IPU1Status.ChainMode = (ipu1ch.chcr.TAG >> 12) & 0x7;
			IPU1Status.InProgress = true;
			if ((ipu1ch.chcr.tag().ID == TAG_REFE) || (ipu1ch.chcr.tag().ID == TAG_END) || (ipu1ch.chcr.tag().IRQ && ipu1ch.chcr.TIE))
			{
				IPU1Status.DMAFinished = true;
			}
			else
			{
				IPU1Status.DMAFinished = false;
			}
		}

		IPU1Status.DMAMode = DMA_MODE_CHAIN;
		IPU1dma();
	}
	else //Normal Mode
	{
			IPU_LOG("Setting up IPU1 Normal mode");
			IPU1Status.InProgress = true;
			IPU1Status.DMAFinished = true;
			IPU1Status.DMAMode = DMA_MODE_NORMAL;
			IPU1dma();
	}
}

extern void GIFdma();

void ipu0Interrupt()
{
	IPU_LOG("ipu0Interrupt: %x", cpuRegs.cycle);

	if(ipu0ch.qwc > 0)
	{
		IPU0dma();
		return;
	}
	if (g_nDMATransfer.FIREINT0)
	{
		g_nDMATransfer.FIREINT0 = false;
		hwIntcIrq(INTC_IPU);
	}

	if (g_nDMATransfer.GIFSTALL)
	{
		// gif
		//log_cb(RETRO_LOG_DEBUG, "IPU GIF Stall\n");
		g_nDMATransfer.GIFSTALL = false;
		//if (gif->chcr.STR) GIFdma();
	}

	if (g_nDMATransfer.VIFSTALL)
	{
		// vif
		//log_cb(RETRO_LOG_WARN, "IPU VIF Stall\n");
		g_nDMATransfer.VIFSTALL = false;
		//if (vif1ch.chcr.STR) dmaVIF1();
	}

	if (g_nDMATransfer.SIFSTALL)
	{
		// sif
		//log_cb(RETRO_LOG_DEBUG, "IPU SIF Stall\n");
		g_nDMATransfer.SIFSTALL = false;

		// Not totally sure whether this needs to be done or not, so I'm
		// leaving it commented out for the moment.
		//if (sif1ch.chcr.STR) SIF1Dma();
	}

	if (g_nDMATransfer.TIE0)
	{
		g_nDMATransfer.TIE0 = false;
	}

	ipu0ch.chcr.STR = false;
	hwDmacIrq(DMAC_FROM_IPU);
	DMA_LOG("IPU0 DMA End");
}

IPU_FORCEINLINE void ipu1Interrupt()
{
	IPU_LOG("ipu1Interrupt %x:", cpuRegs.cycle);

	if(!IPU1Status.DMAFinished || IPU1Status.InProgress)  //Sanity Check
	{
		IPU1dma();
		return;
	}

	DMA_LOG("IPU1 DMA End");
	ipu1ch.chcr.STR = false;
	IPU1Status.DMAMode = DMA_MODE_INTERLEAVE;
	hwDmacIrq(DMAC_TO_IPU);
}
