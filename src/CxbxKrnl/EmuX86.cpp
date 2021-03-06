// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuX86.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *  (c) 2016 Luke Usher <luke.usher@outlook.com>
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_DEFEXTRN_

#include <Zydis.hpp>

#include "CxbxKrnl.h"
#include "Emu.h"
#include "EmuX86.h"
#include "EmuNV2A.h"

uint32_t EmuX86_Read32(uint32_t addr)
{
	if (addr >= 0xFD000000 && addr <= 0xFE000000) {
		return EmuNV2A_Read32(addr & 0x00FFFFFF);
	}

	EmuWarning("EmuX86_Read32: Unknown Read Address %08X", addr);
	return 0;
}

void EmuX86_Write32(uint32_t addr, uint32_t value)
{
	if (addr >= 0xFD000000 && addr <= 0xFE000000) {
		EmuNV2A_Write32(addr & 0x00FFFFFF, value);
		return;
	}

	EmuWarning("EmuX86_Write32: Unknown Write Address %08X (value %08X)", addr, value);
}

DWORD* EmuX86_GetRegisterPointer(LPEXCEPTION_POINTERS e, Zydis::Register reg)
{
	switch (reg) {
		case Zydis::Register::EAX:
			return &e->ContextRecord->Eax;
		case Zydis::Register::EBX:
			return &e->ContextRecord->Ebx;
		case Zydis::Register::ECX:
			return &e->ContextRecord->Ecx;
		case Zydis::Register::EDX:
			return &e->ContextRecord->Edx;
		case Zydis::Register::EDI:
			return &e->ContextRecord->Edi;
		case Zydis::Register::ESI:
			return &e->ContextRecord->Esi;
		case Zydis::Register::EBP:
			return &e->ContextRecord->Ebp;
	}

	return nullptr;
}

bool EmuX86_GetRegisterValue(uint32_t* output, LPEXCEPTION_POINTERS e, Zydis::Register reg)
{
	uint32_t value = 0;

	if (reg != Zydis::Register::NONE)
	{
		DWORD* regptr = EmuX86_GetRegisterPointer(e, reg);
		if (regptr == nullptr)
			return false;

		value = *regptr;
	}

	*output = value;
	return true;
}

bool EmuX86_DecodeMemoryOperand(uint32_t* output, LPEXCEPTION_POINTERS e, Zydis::OperandInfo& operand)
{
	uint32_t base = 0;
	uint32_t index = 0;
	
	if (!EmuX86_GetRegisterValue(&base, e, operand.base) || !EmuX86_GetRegisterValue(&index, e, operand.base)) {
		return false;
	}

	*output = base + (index * operand.scale) + operand.lval.udword;
	return true;
}

bool EmuX86_MOV(LPEXCEPTION_POINTERS e, Zydis::InstructionInfo& info)
{
	if (info.operand[0].type == Zydis::OperandType::REGISTER && info.operand[1].type == Zydis::OperandType::MEMORY) 
	{
		DWORD* pDstReg = nullptr;
		uint32_t srcAddr = 0;

		pDstReg = EmuX86_GetRegisterPointer(e, info.operand[0].base);
		if (pDstReg == nullptr) {
			return false;
		}

		if (!EmuX86_DecodeMemoryOperand(&srcAddr, e, info.operand[1])) {
			return false;
		}
		*pDstReg = EmuX86_Read32(srcAddr);
		return true;
	}
	else if (info.operand[0].type == Zydis::OperandType::MEMORY && info.operand[1].type == Zydis::OperandType::REGISTER)
	{
		uint32_t addr = 0;
		uint32_t value = 0;

		if (!EmuX86_DecodeMemoryOperand(&addr, e, info.operand[0])) {
			return false;
		}

		if (!EmuX86_GetRegisterValue(&value, e, info.operand[1].base)) {
			return false;
		}

		EmuX86_Write32(addr, value);
		return true;
	}
	else if (info.operand[0].type == Zydis::OperandType::MEMORY && info.operand[1].type == Zydis::OperandType::IMMEDIATE)
	{
		uint32_t addr = 0;

		if (!EmuX86_DecodeMemoryOperand(&addr, e, info.operand[0])) {
			return false;
		}

		EmuX86_Write32(addr, info.operand[1].lval.udword);
		return true;
	}

	return false;
}

void EmuX86_SetFlag(LPEXCEPTION_POINTERS e, int flag, int value)
{
	e->ContextRecord->EFlags ^= (-value ^ e->ContextRecord->EFlags) & (1 << flag);
}

bool EmuX86_TEST(LPEXCEPTION_POINTERS e, Zydis::InstructionInfo& info)
{
	if (info.operand[0].type == Zydis::OperandType::MEMORY && info.operand[1].type == Zydis::OperandType::IMMEDIATE)
	{
		uint32_t addr = 0;

		if (!EmuX86_DecodeMemoryOperand(&addr, e, info.operand[0])) {
			return false;
		}

		uint32_t value = EmuX86_Read32(addr);

		// Perform bitwise AND
		uint32_t result = value & info.operand[1].lval.udword;

		// Set CF/OF to 0
		EmuX86_SetFlag(e, EMUX86_EFLAG_CF, 0);
		EmuX86_SetFlag(e, EMUX86_EFLAG_OF, 0);

		EmuX86_SetFlag(e, EMUX86_EFLAG_SF, result >> 31);
		EmuX86_SetFlag(e, EMUX86_EFLAG_ZF, result == 0 ? 1 : 0);

		// TODO: Parity Flag

		return true;
	}

	return false;
}

bool EmuX86_DecodeException(LPEXCEPTION_POINTERS e)
{
	// Only decode instructions within Xbox memory space
	if (e->ContextRecord->Eip > XBOX_MEMORY_SIZE || e->ContextRecord->Eip < 0x10000) {
		return false;
	}

	Zydis::MemoryInput input((uint8_t*)e->ContextRecord->Eip, XBOX_MEMORY_SIZE - e->ContextRecord->Eip);
	Zydis::InstructionInfo info;
	Zydis::InstructionDecoder decoder;
	decoder.setDisassemblerMode(Zydis::DisassemblerMode::M32BIT);
	decoder.setDataSource(&input);
	decoder.setInstructionPointer(e->ContextRecord->Eip);
	Zydis::IntelInstructionFormatter formatter;

	// Decode a single instruction
	decoder.decodeInstruction(info);
	
	if (info.flags & Zydis::IF_ERROR_MASK)
	{
		EmuWarning("EmuX86: Error decoding opcode at 0x%08X\n", e->ContextRecord->Eip);
	}
	else
	{
		switch (info.mnemonic) 
		{	
			case Zydis::InstructionMnemonic::MOV:
				if (EmuX86_MOV(e, info)) {
					break;
				}

				goto unimplemented_opcode;
			case Zydis::InstructionMnemonic::TEST:
				if (EmuX86_TEST(e, info)) {
					break;
				}

				goto unimplemented_opcode;
			case Zydis::InstructionMnemonic::WBINVD:
				// We can safely ignore this
				break;
		}

		e->ContextRecord->Eip += info.length;
		return true;

unimplemented_opcode:
		EmuWarning("EmuX86: 0x%08X: %s Not Implemented\n", e->ContextRecord->Eip, formatter.formatInstruction(info));
		e->ContextRecord->Eip += info.length;
		return false;
	}

	return false;
}
