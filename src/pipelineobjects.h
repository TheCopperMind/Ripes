#ifndef PIPELINEOBJECTS_H
#define PIPELINEOBJECTS_H

#include "binutils.h"
#include "defines.h"
#include "mainmemory.h"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>
#include "assert.h"

/* PIPELINE OBJECTS
 * all pipeline objects are built around the Signal class - a boolean vector of immutable size.
 * The Signal class ensures size compatability of I/O signals, much like regular HDL languages
 * Signals can be cast to signed/unsigned integers or boolean values, to easily use them in ie. computational expression
 * or logic expressions
 *
 * All combinatorial components have an update() function, which corresponds to propagating the
 * input signal values through the combinatorial circuit
 *
 * Registers have a clock() function, which sets the output value to the input value
 */

// Signal class
// A boolean vector of immutable size
// Can be cast to booleans, u/integers etc.
#define ASSERT_SIZE static_assert(n >= 1 && n <= 64, "n = [1;64]");
#define CREATE_VEC m_value = std::vector<bool>(n);
#define SETNAME m_name = name;

template <int n>
class Signal {
public:
    // Constructors
    Signal(std::string name = ""){ASSERT_SIZE CREATE_VEC SETNAME};
    Signal(std::vector<bool> v, std::string name = "") {
        ASSERT_SIZE
        // Builds a signal from a std::vector
        SETNAME
        if (v.size() != n) {
            throw std::invalid_argument("Input vector size does not match Signal size");
        }
        m_value = v;
    }
    Signal(uint32_t v, std::string name = "") {
        ASSERT_SIZE
        CREATE_VEC
        SETNAME
        buildVec(m_value, v);
    }
    Signal(int v, std::string name = "") {
        ASSERT_SIZE
        CREATE_VEC
        SETNAME
        buildVec(m_value, (uint32_t)v);
    }

    void setName(std::string name) { m_name = name; }

    // Casting operators
    explicit operator int() const { return signextend<int32_t, n>(accBVec(m_value)); }
    explicit operator uint32_t() const { return accBVec(m_value); }
    explicit operator bool() const { return m_value[0]; }

private:
    std::vector<bool> value() const { return m_value; }
    std::vector<bool> m_value;
    std::string m_name;
};

// The Reg class is used for sequential signal assignment. Has a single input/output of a given signal size.
// Upon construction, new registers are added to "registers", which is used when clocking the pipeline
class RegBase {
public:
    static std::vector<RegBase*> registers;
    static void clockAll() {
        // Each registers input value is save before clocking it, to ensure that register -> register connections are
        // clocked properly
        std::for_each(registers.begin(), registers.end(), [](auto& reg) { reg->save(); });
        std::for_each(registers.begin(), registers.end(), [](auto& reg) { reg->clock(); });
    }

    static void resetAll() {
        std::for_each(registers.begin(), registers.end(), [](auto& reg) { reg->reset(); });
    }

protected:
    virtual void clock() = 0;
    virtual void save() = 0;
    virtual void reset() = 0;
};

template <int n>
class Reg : public RegBase {
public:
    Reg() {
        ASSERT_SIZE
        registers.push_back(this);
    }

    // Signal connection
    void connect(Reg<n>& r) { setInput(&r.m_current); }
    void connect(const Signal<n>* s) { setInput(s); }
    Signal<n>* getOutput() { return &m_current; }

    explicit operator int() const { return (int)m_current; }
    explicit operator uint32_t() const { return (uint32_t)m_current; }
    explicit operator bool() const { return (bool)m_current; }
    void setInput(const Signal<n>* in) { m_next = in; }

protected:
    void clock() override { m_current = m_nextSaved; }
    void save() override {
        assert(m_next != nullptr);
        m_nextSaved = *m_next;
    }
    void reset() override {
        m_current = Signal<n>(0);
        m_nextSaved = Signal<n>(0);
    }

private:
    Signal<n> m_current;
    Signal<n> m_nextSaved;
    const Signal<n>* m_next;
};

// Multiplexor class
// the output of the multiplexor is dependant of the input select signal
template <int inputs, int n>
class Combinational {
public:
    Combinational() { static_assert(n >= 1 && n <= 32 && inputs > 0, "Invalid multiplexer size specification"); }

    virtual void update() = 0;
    bool setInput(int input, const Signal<n>* sig) {
        if (input < 0 || input >= inputs)
            return false;
        m_inputs[input] = sig;
        return true;
    }

    void setControl(const Signal<bitcount(inputs)>* sig) { m_control = sig; }

    Signal<n>* getOutput() { return &m_output; }

protected:
    std::vector<const Signal<n>*> m_inputs = std::vector<const Signal<n>*>(inputs);
    const Signal<bitcount(inputs)>* m_control;  // control size = roof(log2(inputs))
    Signal<n> m_output;

    bool initialized() {
        bool b = true;
        b &= m_control != nullptr;
        for (const auto& input : m_inputs) {
            b &= input != nullptr;
        }
        return b;
    }
};

template <int inputs, int n>
class Mux : public Combinational<inputs, n> {
public:
    void update() override {
        {
            if (!this->initialized())
                throw std::runtime_error("Mux not initialized");
            this->m_output = *this->m_inputs[(uint32_t) * this->m_control];
        }
    }
};
enum class GateType { AND, OR, XOR };
template <int inputs, int n, GateType type>
class Gate : public Combinational<inputs, n> {
    // Sets an operand functor according to the input GateType, which is used in computing the output of the gate
    // Currently, only 1-bit evaluation is supported
public:
    Gate() {
        switch (type) {
            case GateType::AND:
                m_op = [](bool a, const Signal<n>& b) { return a & (bool)b; };
                break;
            case GateType::OR:
                m_op = [](bool a, const Signal<n>& b) { return a | (bool)b; };
                break;
            case GateType::XOR:
                m_op = [](bool a, const Signal<n>& b) { return a ^ (bool)b; };
                break;
        }
    }
    void update() override {
        bool b = true;
        for (const auto& input : this->m_inputs) {
            b = m_op(b, *input);
        }
        this->m_output = Signal<n>(b);
    }

private:
    std::function<bool(bool, const Signal<n>&)> m_op;
};

namespace ALUDefs {
static const int CTRL_SIZE = 5;
enum OPCODE { ADD, SUB, MUL, DIV, AND, OR, XOR, SL, SRA, SRL, LUI, LT /*Less than*/, LTU /*Less than Unsigned*/, EQ };
}  // namespace ALUDefs

template <int n>
class ALU {
public:
    // When calculating ALU control signals, the OPCODE is implicitely converted to an int in the Signal, which is
    // later reinterpreted as an OPCODE
    ALU(std::string name = "ALU") { m_name = name; }
    void update();

    void setInputs(Signal<n>* s1, Signal<n>* s2) {
        m_op1 = s1;
        m_op2 = s2;
    }

    Signal<n>* getOutput() { return &m_output; }

    void setControl(const Signal<ALUDefs::CTRL_SIZE>* sig) { m_control = sig; }

private:
    bool initialized() {
        bool b = true;
        b &= m_control != nullptr;
        b &= m_op1 != nullptr;
        b &= m_op2 != nullptr;
        return b;
    }
    std::string m_name;
    Signal<n> m_output;
    const Signal<n>* m_op1;
    const Signal<n>* m_op2;
    const Signal<ALUDefs::CTRL_SIZE>* m_control;
};

template <int n>
void ALU<n>::update() {
    if (!initialized())
        throw std::runtime_error("Mux not initialized");
    switch ((ALUDefs::OPCODE)(int)*m_control) {
        case ALUDefs::ADD:
            m_output = (uint32_t)*m_op1 + (uint32_t)*m_op2;
            break;
        case ALUDefs::SUB:
            m_output = (uint32_t)*m_op1 - (uint32_t)*m_op2;
            break;
        case ALUDefs::MUL:
            m_output = (uint32_t)*m_op1 * (uint32_t)*m_op2;
            break;
        case ALUDefs::DIV:
            m_output = (uint32_t)*m_op1 / (uint32_t)*m_op2;
            break;
        case ALUDefs::AND:
            m_output = (uint32_t)*m_op1 & (uint32_t)*m_op2;
            break;
        case ALUDefs::OR:
            m_output = (uint32_t)*m_op1 | (uint32_t)*m_op2;
            break;
        case ALUDefs::XOR:
            m_output = (uint32_t)*m_op1 ^ (uint32_t)*m_op2;
            break;
        case ALUDefs::SL:
            m_output = (uint32_t)*m_op1 << (uint32_t)*m_op2;
            break;
        case ALUDefs::SRA:
            m_output = (uint32_t)*m_op1 >> (uint32_t)*m_op2;
            break;
        case ALUDefs::SRL:
            m_output = (int)*m_op1 + (uint32_t)*m_op2;
            break;
        case ALUDefs::LUI:
            m_output = (uint32_t)*m_op2;
            break;
        case ALUDefs::LT:
            m_output = (int)*m_op1 < (int)*m_op2 ? 1 : 0;
            break;
        case ALUDefs::LTU:
            m_output = (uint32_t)*m_op1 < (uint32_t)*m_op2 ? 1 : 0;
            break;
        default:
            throw std::runtime_error("Invalid ALU opcode");
            break;
    }
}

class Registers {
public:
    Registers() {}

    std::vector<uint32_t>* getRegPtr() { return &m_reg; }
    void update();
    void clock();
    void clear() {
        for (auto& reg : m_reg)
            reg = 0;
    }
    void init();

    void setInputs(Signal<32>* instr, Signal<5>* writeReg, Signal<32>* writeData, Signal<1>* regWrite);
    Signal<32>* getOutput(int n = 1) { return n == 2 ? &m_readData2 : &m_readData1; }

private:
    // ReadRegister 1/2 is deduced from the input instruction signal
    const Signal<1>* m_regWrite;
    const Signal<32>* m_instr;
    const Signal<5>* m_writeRegister;
    const Signal<32>* m_writeData;
    Signal<32> m_readData1;
    Signal<32> m_readData2;

    std::vector<uint32_t> m_reg = std::vector<uint32_t>(32, 0);  // Internal registers

    std::string m_name = std::string("Registers");
};

#endif  // PIPELINEOBJECTS_H
