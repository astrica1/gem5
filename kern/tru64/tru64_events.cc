/*
 * Copyright (c) 2003 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/exec_context.hh"
#include "cpu/base_cpu.hh"
#include "cpu/full_cpu/bpred.hh"
#include "cpu/full_cpu/full_cpu.hh"
#include "kern/tru64/dump_mbuf.hh"
#include "kern/tru64/printf.hh"
#include "kern/tru64/tru64_events.hh"
#include "mem/functional_mem/memory_control.hh"
#include "targetarch/arguments.hh"

#ifdef FS_MEASURE
#include "sim/system.hh"
#include "sim/sw_context.hh"
#endif

void
SkipFuncEvent::process(ExecContext *xc)
{
    Addr newpc = xc->regs.intRegFile[ReturnAddressReg];

    DPRINTF(PCEvent, "skipping %s: pc=%x, newpc=%x\n", description,
            xc->regs.pc, newpc);

    xc->regs.pc = newpc;
    xc->regs.npc = xc->regs.pc + sizeof(MachInst);

    BranchPred *bp = xc->cpu->getBranchPred();
    if (bp != NULL) {
        bp->popRAS(xc->thread_num);
    }
}

void
BadAddrEvent::process(ExecContext *xc)
{
    // The following gross hack is the equivalent function to the
    // annotation for vmunix::badaddr in:
    // simos/simulation/apps/tcl/osf/tlaser.tcl

    uint64_t a0 = xc->regs.intRegFile[ArgumentReg0];

    if (a0 < ALPHA_K0SEG_BASE || a0 >= ALPHA_K1SEG_BASE ||
        xc->memCtrl->badaddr(ALPHA_K0SEG_TO_PHYS(a0) & PA_IMPL_MASK)) {

        DPRINTF(BADADDR, "badaddr arg=%#x bad\n", a0);
        xc->regs.intRegFile[ReturnValueReg] = 0x1;
        SkipFuncEvent::process(xc);
    }
    else
        DPRINTF(BADADDR, "badaddr arg=%#x good\n", a0);
}

void
PrintfEvent::process(ExecContext *xc)
{
    if (DTRACE(Printf)) {
        DebugOut() << curTick << ": " << xc->cpu->name() << ": ";

        AlphaArguments args(xc);
        tru64::Printf(args);
    }
}

void
DebugPrintfEvent::process(ExecContext *xc)
{
    if (DTRACE(DebugPrintf)) {
        if (!raw)
            DebugOut() << curTick << ": " << xc->cpu->name() << ": ";

        AlphaArguments args(xc);
        tru64::Printf(args);
    }
}

void
DumpMbufEvent::process(ExecContext *xc)
{
    if (DTRACE(DebugPrintf)) {
        AlphaArguments args(xc);
        tru64::DumpMbuf(args);
    }
}

#ifdef FS_MEASURE
FnEvent::FnEvent(PCEventQueue *q, const std::string & desc, System *system)
    : PCEvent(q, desc), _name(desc)
{
    myBin = system->getBin(desc);
    assert(myBin);
}

void
FnEvent::process(ExecContext *xc)
{
    if (xc->misspeculating())
        return;
    assert(xc->system->bin && "FnEvent must be in a binned system");
    SWContext *ctx = xc->swCtx;
    DPRINTF(TCPIP, "%s: %s Event!!!\n", xc->system->name(), description);

    if (ctx && !ctx->callStack.empty()) {
        fnCall *last = ctx->callStack.top();
        if (!xc->system->findCaller(myname(), last->name)) {
            // assert(!xc->system->findCaller(myname(), "")  &&
            //     "should not have head of path in middle of stack!");
            return;
        }
        ctx->calls--;
    } else {
        if (!xc->system->findCaller(myname(), "")) {
            return;
        }
        if (!ctx)  {
            DPRINTF(TCPIP, "creating new context for %s\n", myname());
            ctx = new SWContext;
            xc->swCtx = ctx;
        }
    }
    DPRINTF(TCPIP, "adding fn %s to context\n", myname());
    fnCall *call = new fnCall;
    call->myBin = myBin;
    call->name = myname();
    ctx->callStack.push(call);
    myBin->activate();
    xc->system->fnCalls++;
    xc->system->dumpState(xc);
}
#endif //FS_MEASURE
