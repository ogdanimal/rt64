//
// RT64
//

#include "rt64_interpreter.h"

#include <cassert>

//#define DUMP_DISPLAY_LISTS

namespace RT64 {
    static FILE *displayListFp = nullptr;

    // Interpreter

    Interpreter::Interpreter() {
        state = nullptr;
        hleGBI = nullptr;
        extendedFunction = gbiManager.getExtendedFunction();
    }

    void Interpreter::setup(State *state) {
        this->state = state;
    }

    void Interpreter::loadUCodeGBI(uint32_t textAddress, uint32_t dataAddress, bool resetFromTask) {
        if (!resetFromTask) {
            state->flush();
        }

        const uint32_t AddressMask = 0xFFFFF8;
        const uint32_t maskedTextAddress = textAddress & AddressMask;
        const uint32_t maskedDataAddress = dataAddress & AddressMask;
        if ((UCode.textAddress != maskedTextAddress) || (UCode.dataAddress != maskedDataAddress)) {
            hleGBI = gbiManager.getGBIForUCode(state->RDRAM, maskedTextAddress, maskedDataAddress);
            if (hleGBI != nullptr) {
                state->rsp->setGBI(hleGBI);
            }

            UCode.textAddress = maskedTextAddress;
            UCode.dataAddress = maskedDataAddress;
        }

        if (hleGBI != nullptr) {
            GBIReset resetFunction = resetFromTask ? hleGBI->resetFromTask : hleGBI->resetFromLoad;
            if (resetFunction != nullptr) {
                resetFunction(state);
            }
        }
    }

    void Interpreter::processRDPLists(uint32_t dlStartAdddress, DisplayList *dlStart, DisplayList *dlEnd) {
        state->dlCpuProfiler.start();

        // Update the state with the current display list address.
        state->displayListAddress = dlStartAdddress;
        state->displayListCounter++;

        // Check RDRAM if required.
        state->checkRDRAM();

        GBI *rdpGBI = state->rdp->gbi;
        constexpr unsigned int opCodeMask = 0x3F;

        // Run the command interpreter.
        assert(rdpGBI != nullptr);
        DisplayList *dl = dlStart;
        uint8_t opCode;
        GBIFunction func;
        uint32_t cmdLength;
        size_t pendingCommandRemainingBytes = state->rdp->pendingCommandRemainingBytes;

        if (dlStart >= dlEnd) {
            state->dlCpuProfiler.end();
            return;
        }

        if (pendingCommandRemainingBytes != 0) {
            // Copy the remaining command bytes from the current displaylist
            uint32_t toCopy = (uint32_t)std::min(pendingCommandRemainingBytes, (uintptr_t)dlEnd - (uintptr_t)dl);
            memcpy(state->rdp->pendingCommandBuffer.data() + state->rdp->pendingCommandCurrentBytes, dl, toCopy);

            // Modify start to skip the copied bytes
            dl = (DisplayList *)(toCopy + (uintptr_t)dl);

            // Check if we've copied all of the bytes of the command into the buffer
            if (pendingCommandRemainingBytes == toCopy) {
                // All bytes have been copied, so run the completed command
                DisplayList *pendingCommand = (DisplayList *)state->rdp->pendingCommandBuffer.data();
                opCode = (pendingCommand->w0 >> 24) & opCodeMask;
                func = rdpGBI->map[opCode];

                if (func != nullptr) {
                    func(state, &pendingCommand);
                }
                else {
                    RT64_LOG_PRINTF("DL Parser ran into an unknown RDP opCode: %u / 0x%X", opCode, opCode);
                }

                state->rdp->pendingCommandCurrentBytes = 0;
                state->rdp->pendingCommandRemainingBytes = 0;
            }
            // Not all of the bytes were copied, so adjust RDP state accordingly and exit.
            else {
                state->rdp->pendingCommandCurrentBytes += toCopy;
                state->rdp->pendingCommandRemainingBytes -= toCopy;
                state->dlCpuProfiler.end();
                return;
            }
        }

        // Create a dummy pointer and pass that, since displaylist pointer incrementing is handled differently in LLE.
        DisplayList *dummy;
        while ((dl != nullptr) && ((dlEnd == nullptr) || (dl < dlEnd))) {
            opCode = (dl->w0 >> 24) & opCodeMask;

            if ((extendedOpCode != 0) && (opCode == extendedOpCode)) {
                dummy = dl;
                extendedFunction(state, &dl);
                cmdLength = 1;
            }
            else {
                func = rdpGBI->map[opCode];
                cmdLength = state->rdp->commandWordLengths[opCode];

#       ifdef DUMP_DISPLAY_LISTS
                RT64_LOG_PRINTF("0x%08X 0x%08X", dl->w0, dl->w1);
#       endif

                // Check if this command is unfinished and store the partial contents if so.
                if (dl + cmdLength > dlEnd) {
                    uint32_t toCopy = (uint32_t)((uintptr_t)dlEnd - (uintptr_t)dl);
                    memcpy(state->rdp->pendingCommandBuffer.data(), dl, toCopy);
                    state->rdp->pendingCommandCurrentBytes = toCopy;
                    state->rdp->pendingCommandRemainingBytes = cmdLength * sizeof(DisplayList) - toCopy;
                    break;
                }

                if (func != nullptr) {
                    dummy = dl;
                    func(state, &dummy);
                }
                else {
                    RT64_LOG_PRINTF("DL Parser ran into an unknown RDP opCode: %u / 0x%X", opCode, opCode);
                }
            }

            if (dl != nullptr) {
                dl += cmdLength;
            }
        }

        state->dlCpuProfiler.end();
    }

    void Interpreter::processDisplayLists(uint32_t dlStartAdddress, DisplayList *dlStart) {
        assert(hleGBI != nullptr);

        state->dlCpuProfiler.start();

        // Update the state with the current display list address.
        state->displayListAddress = dlStartAdddress;
        state->displayListCounter++;

        // Check RDRAM if required.
        state->checkRDRAM();

        // Run the command interpreter.
        //
        // The walk below ends only when a command sets `dl` to null -- a
        // G_ENDDL. Nothing else bounds it: there is no command limit, no
        // address range check, and an unrecognised opcode logs and then
        // advances rather than stopping. A list that never terminates, or whose
        // pointer wanders out of the list and into data, therefore executes
        // memory as commands forever, and every command it runs appends to the
        // state's buffers on the way.
        //
        // That is not a slow frame, it is a hang plus a ~490 MB/s memory leak
        // on the gfx thread, and it is what took the WSL VM down repeatedly
        // while Hybrid Heaven sat wedged at its expansion pak screen. The gfx
        // thread never returns here, so its action queue grows at the VI
        // thread's 60 Hz while RSS climbs until the OOM killer fires.
        //
        // A real frame is thousands of commands, so this ceiling is orders of
        // magnitude above any legitimate list and cannot truncate valid
        // content. Stopping abandons one frame; not stopping loses the process.
        constexpr uint64_t MaxDisplayListCommands = 1ULL << 20;
        uint64_t commandCount = 0;

        DisplayList *dl = dlStart;
        uint8_t opCode;
        GBIFunction func;
        while (dl != nullptr) {
            if (++commandCount > MaxDisplayListCommands) {
                fprintf(stderr, "[rt64] display list at 0x%08X exceeded %llu commands without ending -- "
                                "abandoning the frame (runaway display list)\n",
                        dlStartAdddress, (unsigned long long)(MaxDisplayListCommands));
                break;
            }

            opCode = (dl->w0 >> 24);

            if ((extendedOpCode != 0) && (opCode == extendedOpCode)) {
                extendedFunction(state, &dl);
            }
            else {
                func = hleGBI->map[opCode];

#       ifdef DUMP_DISPLAY_LISTS
                RT64_LOG_PRINTF("0x%08X 0x%08X", dl->w0, dl->w1);
#       endif

                if (func != nullptr) {
                    func(state, &dl);
                }
                else {
                    RT64_LOG_PRINTF("DL Parser ran into an unknown opCode (GBI %u): %u / 0x%X", uint32_t(hleGBI->ucode), opCode, opCode);
                }
            }

            if (dl != nullptr) {
                dl++;
            }
        }

        state->dlCpuProfiler.end();
    }
};
