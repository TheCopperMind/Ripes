#pragma once

#include "VSRTL/core/vsrtl_component.h"

#include "riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class Immediate : public Component {
public:
    Immediate(std::string name, SimComponent* parent) : Component(name, parent) {
        imm << [=] {
            Switch(opcode, RVInstr) {
                case RVInstr::LUI:
                case RVInstr::AUIPC:
                    return instr.uValue() & 0xfffff000;
                case RVInstr::JAL: {
                    const auto fields = RVInstrParser::getParser()->decodeJ32Instr(instr.uValue());
                    return VT_U(
                        signextend<int32_t, 21>(fields[0] << 20 | fields[1] << 1 | fields[2] << 11 | fields[3] << 12));
                }
                case RVInstr::JALR: {
                    return VT_U(signextend<int32_t, 12>((instr.uValue() >> 20)));
                }
                case RVInstr::BEQ:
                case RVInstr::BNE:
                case RVInstr::BLT:
                case RVInstr::BGE:
                case RVInstr::BLTU:
                case RVInstr::BGEU: {
                    const auto fields = RVInstrParser::getParser()->decodeB32Instr(instr.uValue());
                    return VT_U(signextend<int32_t, 13>((fields[0] << 12) | (fields[1] << 5) | (fields[5] << 1) |
                                                        (fields[6] << 11)));
                }
                case RVInstr::LB:
                case RVInstr::LH:
                case RVInstr::LW:
                case RVInstr::LBU:
                case RVInstr::LHU:
                case RVInstr::ADDI:
                case RVInstr::SLTI:
                case RVInstr::SLTIU:
                case RVInstr::XORI:
                case RVInstr::ORI:
                case RVInstr::ANDI:
                case RVInstr::SLLI:
                case RVInstr::SRLI:
                case RVInstr::SRAI:
                    return VT_U(signextend<int32_t, 12>((instr.uValue() >> 20)));
                case RVInstr::SB:
                case RVInstr::SH:
                case RVInstr::SW: {
                    return VT_U(signextend<int32_t, 12>(((instr.uValue() & 0xfe000000)) >> 20) |
                                ((instr.uValue() & 0xf80) >> 7));
                }
                default:
                    return VT_U(0xDEADBEEF);
            }
        };
    }

    INPUTPORT_ENUM(opcode, RVInstr);
    INPUTPORT(instr, XLEN);
    OUTPUTPORT(imm, XLEN);
};

}  // namespace core
}  // namespace vsrtl
