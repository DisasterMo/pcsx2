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

#include "GS.h"
#include "Gif.h"
#include "Gif_Unit.h"
#include "Vif.h"
#include "Vif_Dma.h"
#include "IPU/IPU.h"
#include "IPU/IPU_Fifo.h"

//////////////////////////////////////////////////////////////////////////
/////////////////////////// Quick & dirty FIFO :D ////////////////////////
//////////////////////////////////////////////////////////////////////////

// Notes on FIFO implementation
//
// The FIFO consists of four separate pages of HW register memory, each mapped to a
// PS2 device.  They are listed as follows:
//
// 0x4000 - 0x5000 : VIF0  (all registers map to 0x4000)
// 0x5000 - 0x6000 : VIF1  (all registers map to 0x5000)
// 0x6000 - 0x7000 : GS    (all registers map to 0x6000)
// 0x7000 - 0x8000 : IPU   (registers map to 0x7000 and 0x7010, respectively)

void __fastcall ReadFIFO_VIF1(mem128_t* out)
{
#ifndef NDEBUG
	if (vif1Regs.stat.test(VIF1_STAT_INT | VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS) )
		log_cb(RETRO_LOG_DEBUG, "Reading from vif1 fifo when stalled\n" );
#endif

	ZeroQWC(out); // Clear first in case no data gets written...
	//pxAssertRel(vif1Regs.stat.FQC != 0, "FQC = 0 on VIF FIFO READ!");
	if (vif1Regs.stat.FDR) {
#ifndef NDEBUG
		if (vif1Regs.stat.FQC > vif1.GSLastDownloadSize) {
			log_cb(RETRO_LOG_DEBUG, "Warning! GS Download size < FIFO count!\n");
		}
#endif
		if (vif1Regs.stat.FQC > 0) {
			GetMTGS().WaitGS();
			GetMTGS().SendPointerPacket(GS_RINGTYPE_INIT_READ_FIFO1, 0, out);
			GetMTGS().WaitGS(false); // wait without reg sync
			GSreadFIFO((u64*)out);
			vif1.GSLastDownloadSize--;
			GUNIT_LOG("ReadFIFO_VIF1");
			if (vif1.GSLastDownloadSize <= 16)
				gifRegs.stat.OPH = false;
			vif1Regs.stat.FQC = std::min((u32)16, vif1.GSLastDownloadSize);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// WriteFIFO Pages
//
void __fastcall WriteFIFO_VIF0(const mem128_t *value)
{
	vif0ch.qwc += 1;
#ifndef NDEBUG
	if(vif0.irqoffset.value != 0 && vif0.vifstalled.enabled)
		log_cb(RETRO_LOG_DEBUG, "Offset on VIF0 FIFO start!\n");
#endif
	bool ret = VIF0transfer((u32*)value, 4);

	if (vif0.cmd) 
	{
		if(vif0.done && vif0ch.qwc == 0)	vif0Regs.stat.VPS = VPS_WAITING;
	}
	else		 
		vif0Regs.stat.VPS = VPS_IDLE;

	//pxAssertDev( ret, "vif stall code not implemented" );
}

void __fastcall WriteFIFO_VIF1(const mem128_t *value)
{
#ifndef NDEBUG
	if (vif1Regs.stat.FDR) {
		log_cb(RETRO_LOG_DEBUG, "writing to fifo when fdr is set!\n");
	}
	if (vif1Regs.stat.test(VIF1_STAT_INT | VIF1_STAT_VSS | VIF1_STAT_VIS | VIF1_STAT_VFS) ) {
		log_cb(RETRO_LOG_DEBUG, "writing to vif1 fifo when stalled\n");
	}
	if (vif1.irqoffset.value != 0 && vif1.vifstalled.enabled) {
		log_cb(RETRO_LOG_DEBUG, "Offset on VIF1 FIFO start!\n");
	}
#endif

	bool ret = VIF1transfer((u32*)value, 4);

	if (vif1.cmd) {
		if (vif1.done && !vif1ch.qwc) vif1Regs.stat.VPS = VPS_WAITING;
	}
	else vif1Regs.stat.VPS = VPS_IDLE;

	if( gifRegs.stat.APATH == 2  && gifUnit.gifPath[1].isDone())
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;
		vif1Regs.stat.VGW = false; //Let vif continue if it's stuck on a flush

		if(gifUnit.checkPaths(1,0,1)) gifUnit.Execute(false, true);
	}

	//pxAssertDev( ret, "vif stall code not implemented" );
}

void __fastcall WriteFIFO_GIF(const mem128_t *value)
{
	GUNIT_LOG("WriteFIFO_GIF()");
	if ((!gifUnit.CanDoPath3() || gif_fifo.fifoSize > 0)) {
		//DevCon.Warning("GIF FIFO HW Write");
		gif_fifo.write_fifo((u32*)value, 1);
		gif_fifo.read_fifo();
	}
	else {
		gifUnit.TransferGSPacketData(GIF_TRANS_FIFO, (u8*)value, 16);
	}

	if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT)
		gifUnit.gifPath[GIF_PATH_3].state = GIF_PATH_IDLE;

	if (gifRegs.stat.APATH == 3)
	{
		gifRegs.stat.APATH = 0;
		gifRegs.stat.OPH = 0;

		if (gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_IDLE || gifUnit.gifPath[GIF_PATH_3].state == GIF_PATH_WAIT)
		{
			if (gifUnit.checkPaths(1, 1, 0)) gifUnit.Execute(false, true);
		}

	}
	
}
