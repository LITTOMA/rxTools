#include "NandDumper.h"
#include "common.h"
#include "console.h"
#include "draw.h"
#include "hid.h"
#include "fs.h"
#include "screenshot.h"
#include "padgen.h"
#include "crypto.h"
#include "ncch.h"
#include "CTRDecryptor.h"

#define NAND_SIZE 0x3AF00000
#define NAND_SECTOR_SIZE 0x200
#define BUF1 0x21000000

char myString[256];		//for showing percentages

void NandMenu(){
	f_mkdir ("nand");
	MenuInit(&NandOptions);
	MenuShow();

    while (true) {
        u32 pad_state = InputWait();
		if(pad_state & BUTTON_DOWN) MenuNextSelection();
		if(pad_state & BUTTON_UP)   MenuPrevSelection();
		if(pad_state & BUTTON_A)    MenuSelect();
		if(pad_state & BUTTON_B) 	break;
		TryScreenShot();
		MenuShow();
    }
}

int NandSwitch(){
	ConsoleInit();
	ConsoleAddText("Choose the NAND you want to use\n");
	ConsoleAddText("Press X : SysNAND");
	ConsoleAddText("Press Y : EmuNAND");
	ConsoleAddText("Press B : Back");
	ConsoleShow();
	while (true) {
        u32 pad_state = InputWait();
		if(pad_state & BUTTON_X)    return  0;
		if(pad_state & BUTTON_Y)    return  1;
		if(pad_state & BUTTON_B) 	return -1;
    }
}

void NandDumper(){
	File myFile;
	int isEmuNand = checkEmuNAND() ? NandSwitch() : 0;
	if(isEmuNand == -1) return;

	ConsoleInit();
	ConsoleAddText(isEmuNand ? "EmuNAND Dumper\n" : "NAND Dumper\n");
	unsigned char* buf = 0x21000000;
	unsigned int nsectors = 0x200;  //sectors in a row
	char ProgressBar[] = "[                            ]";
	unsigned int progress = 1;
	int BACKCOLOR = ConsoleGetBackgroundColor();
	if(FileOpen(&myFile, isEmuNand ? "/nand/EMUNAND.bin" : "/nand/NAND.bin", 1)){
		ConsoleAddText("Dumping...\n"); ConsoleShow();
		int x, y; ConsoleGetXY(&x, &y); y += CHAR_WIDTH * 6; x += CHAR_WIDTH*2;
		DrawString(TOP_SCREEN, ProgressBar, x, y, ConsoleGetTextColor(), ConsoleGetBackgroundColor());
		DrawString(TOP_SCREEN, "Press B anytime to abort", x, y + CHAR_WIDTH*2, ConsoleGetTextColor(), ConsoleGetBackgroundColor());

		for(int count = 0; count < NAND_SIZE/NAND_SECTOR_SIZE/nsectors; count++){

			if(isEmuNand) sdmmc_sdcard_readsectors(count*nsectors, nsectors, buf);
			else sdmmc_nand_readsectors(count*nsectors, nsectors, buf);

			FileWrite(&myFile, buf, nsectors*NAND_SECTOR_SIZE, count*NAND_SECTOR_SIZE*nsectors);
			TryScreenShot();
			if((count % (int)(NAND_SIZE/NAND_SECTOR_SIZE/nsectors/25)) == 0 && count != 0){
				DrawString(TOP_SCREEN, "-", x+(CHAR_WIDTH*(progress++)), y, ConsoleGetTextColor(), ConsoleGetBackgroundColor());
			}
			unsigned int pad = GetInput();
			if(pad & BUTTON_B) break;
		}
		if(isEmuNand){
			sdmmc_sdcard_readsectors(checkEmuNAND()/0x200, 1, buf);
			FileWrite(&myFile, buf, 0x200, 0);
		}
		FileClose(&myFile);
		ConsoleAddText("\nFinished dumping!"); ConsoleShow();
	}else{
		ConsoleAddText("Failed to create the dump"); ConsoleShow();
	}
	ConsoleAddText("\nPress A to exit"); ConsoleShow();
	WaitForButton(BUTTON_A);
}

void DumpNandPartitions(){
	int isEmuNand = checkEmuNAND() ? NandSwitch() : 0;
	if(isEmuNand == -1) return;
	char* p_name[] = { "twln.bin", "twlp.bin", "agb_save.bin", "firm0.bin", "firm1.bin", "ctrnand.bin" };
	unsigned int p_size[] = { 0x08FB5200, 0x020B6600, 0x00030000, 0x00400000, 0x00400000, 0x2F3E3600};
	unsigned int p_addr[] = { TWLN, TWLP, AGB_SAVE, FIRM0, FIRM1, CTRNAND };
	int sect_row = 0x80;

	ConsoleInit();
	ConsoleAddText(isEmuNand ? "EmuNAND Partitions Decryptor\n \n" : "NAND Partitions Decryptor\n \n");

	for(int i = 3; i < 6; i++){		//Cutting out twln, twlp and agb_save. Todo: Properly decrypt them
		File out;
		sprintf(myString, isEmuNand ? "nand/emu_%s" : "nand/%s", p_name[i]);
		FileOpen(&out, myString, 1);
		sprintf(myString, "Dumping %s ...", p_name[i]);
		ConsoleAddText(myString);
		ConsoleShow();

		for(int j = 0; j*0x200 < p_size[i]; j += sect_row){
			sprintf(myString, "%08X / %08X", j*0x200, p_size[i]);
			int x, y; ConsoleGetXY(&x, &y); y += CHAR_WIDTH * 4; x += CHAR_WIDTH*2;
			DrawString(TOP_SCREEN, myString, x, y, ConsoleGetTextColor(), ConsoleGetBackgroundColor());

			if(isEmuNand) emunand_readsectors(j, sect_row, BUF1, p_addr[i]);
			else nand_readsectors(j, sect_row, BUF1, p_addr[i]);
			FileWrite(&out, BUF1, sect_row*0x200, j*0x200);
		}
		FileClose(&out);
	}
	ConsoleAddText("\nPress A to exit"); ConsoleShow();
	WaitForButton(BUTTON_A);
}

void GenerateNandXorpads(){
	PadInfo myInfo = {.keyslot = 0x4, .setKeyY = 0, .size_mb = 760, .filename = "nand/nand.fat16.xorpad"};
	GetNANDCTR(myInfo.CTR); add_ctr(myInfo.CTR, 0xB93000);

	ConsoleInit();
	ConsoleAddText("NAND Xorpad Generator\n");
	ConsoleAddText("Generating nand.fat16.xorpad ..."); ConsoleShow();
	CreatePad(&myInfo, 0);

	ConsoleAddText("\nPress A to exit"); ConsoleShow();
	WaitForButton(BUTTON_A);
}

void DumpNANDSystemTitles(){
	int isEmuNand = checkEmuNAND() ? NandSwitch() : 0;
	if(isEmuNand == -1) return;
	char* outfolder = "nand/titles";

	ConsoleInit();
	ConsoleAddText(isEmuNand ? "EmuNAND System Titles Dumper\n" : "NAND System Titles Dumper\n");
	ConsoleAddText("This should take long, the\nentire ctrnand will be scanned.\nBe patient and wait until\nthe research is finished.");
	ConsoleShow();

	File pfile;
	char filename[256];
	int nTitle = 0;
	unsigned int tot_size = 0x179000;
	f_mkdir (outfolder);
	for(int i = 0; i < tot_size; i++){
		if(isEmuNand) emunand_readsectors(i, 1, BUF1, CTRNAND);
		else nand_readsectors(i, 1, BUF1, CTRNAND);
		if(*((char*)BUF1 + 0x100) == 'N' && *((char*)BUF1 + 0x101) == 'C' && *((char*)BUF1 + 0x102) == 'C' && *((char*)BUF1 + 0x103) == 'H'){
			ctr_ncchheader ncch;
			memcpy((void*)&ncch, BUF1, 0x200);
			sprintf(filename, isEmuNand ? "%s/emu_%08X%08X.app" : "%s/%08X%08X.app", outfolder, *((unsigned int*)(BUF1 + 0x10C)), *((unsigned int*)(BUF1 + 0x108)));
			ConsoleInit();
			ConsoleAddText(isEmuNand ? "EmuNAND System Titles Dumper\n" : "NAND System Titles Dumper\n");
			ConsoleAddText("Dumping...");
			ConsoleAddText((char*)filename + strlen(outfolder) + 1);
			ConsoleShow();
			FileOpen(&pfile, filename, 1);
			for(int j = 0; j < getle32(ncch.contentsize); j++){
				if(isEmuNand) emunand_readsectors(i + j, 1, BUF1, CTRNAND);
				else nand_readsectors(i + j, 1, BUF1, CTRNAND);
				FileWrite(&pfile, BUF1, 0x200, j*0x200);
			}
			FileClose(&pfile);
			i += getle32(ncch.contentsize);
			nTitle++;
		}
	}
	ConsoleInit();
	ConsoleAddText(isEmuNand ? "EmuNAND System Titles Dumper\n" : "NAND System Titles Dumper\n");
	sprintf(myString, "The entire ctrnand has been\nscanned and %d CTR titles have\nbeen dumped.\nDo you want to decrypt them\nall with CTRDecryptor?\n", nTitle);
	ConsoleAddText(myString);
	ConsoleAddText("Press A : Decrypt titles");
	ConsoleAddText("Press B : Exit");
	ConsoleShow();
	while (true) {
        u32 pad_state = InputWait();
		if(pad_state & BUTTON_A){
			CTRDecryptor();
			break;
		}
		if(pad_state & BUTTON_B) 	break;
    }
}

void RebuildNand(){
	char* p_name[] = { "twln.bin", "twlp.bin", "agb_save.bin", "firm0.bin", "firm1.bin", "ctrnand.bin" };
	unsigned int p_size[] = { 0x08FB5200, 0x020B6600, 0x00030000, 0x00400000, 0x00400000, 0x2F3E3600};
	unsigned int p_addr[] = { TWLN, TWLP, AGB_SAVE, FIRM0, FIRM1, CTRNAND };
	int sect_row = 0x1;			//Slow, ok, but secure

	ConsoleInit();
	int isEmuNand = checkEmuNAND();
	ConsoleAddText(isEmuNand ? "EmuNAND Partitions Decryptor\n \n" : "NAND Partitions Decryptor\n \n");
	if(!isEmuNand){
		ConsoleAddText("No EmuNAND mounted.");
		ConsoleAddText("\nPress A to exit"); ConsoleShow();
		WaitForButton(BUTTON_A);
		return;
	}
	for(int i = 3; i < 6; i++){		//Cutting out twln, twlp and agb_save. Todo: Properly decrypt them
		File out;
		sprintf(myString, isEmuNand ? "nand/emu_%s" : "nand/%s", p_name[i]);
		if(FileOpen(&out, myString, 0)){
			sprintf(myString, "Injecting %s ...", p_name[i]);
			ConsoleAddText(myString);
			ConsoleShow();

			for(int j = 0; j*0x200 < p_size[i]; j += sect_row){
				sprintf(myString, "%08X / %08X", j*0x200, p_size[i]);
				int x, y; ConsoleGetXY(&x, &y); y += CHAR_WIDTH * 4; x += CHAR_WIDTH*2;
				DrawString(TOP_SCREEN, myString, x, y, ConsoleGetTextColor(), ConsoleGetBackgroundColor());

				FileRead(&out, BUF1, sect_row*0x200, j*0x200);
				if(isEmuNand) emunand_writesectors(j, sect_row, BUF1, p_addr[i]);
			}
			FileClose(&out);
		}
	}
	ConsoleAddText("\nPress A to exit"); ConsoleShow();
	WaitForButton(BUTTON_A);
}
