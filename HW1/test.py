import argparse
import numpy as np
import json
import subprocess

num_registers = 32

def parse_instruction(instruction):
    instruction = instruction.replace(",", "")
    parts = instruction.split()
    if len(parts) != 4:
        raise ValueError(f"Invalid instruction format: {instruction}")
    if parts[0] == "addi":
        parts[-1] = np.int64(parts[-1])
    return parts

class Simulator:
    def __init__(self):
        self.registers = {}
        for i in range(num_registers):
            self.registers[f"x{i}"] = np.uint64(0)
        self.exception = False
        self.exception_pc = 0
        self.pc = 0

        self.__exception = False

    def step(self, instruction: str):
        """
        Execute a single instruction.
        :param instruction: A list containing the instruction components.
        """
        if self.__exception:
            return

        instruction = parse_instruction(instruction)
        opcode, dest, op_a, op_b = instruction
        self.pc += 1

        if opcode == "add":
            self.registers[dest] = self.registers[op_a] + self.registers[op_b]
        elif opcode == "addi":
            self.registers[dest] = self.registers[op_a] + op_b.astype(np.uint64)
        elif opcode == "sub":
            self.registers[dest] = self.registers[op_a] - self.registers[op_b]
        elif opcode == "mulu":
            self.registers[dest] = self.registers[op_a] * self.registers[op_b]
        elif opcode == "divu":
            if self.registers[op_b] == 0:
                self.__exception = True
                self.exception_pc = self.pc - 1
                self.pc = int("0x10000", 16)
            else:
                self.registers[dest] = self.registers[op_a] // self.registers[op_b]
        elif opcode == "remu":
            if self.registers[op_b] == 0:
                self.__exception = True
                self.exception_pc = self.pc - 1
                self.pc = int("0x10000", 16)
            else:
                self.registers[dest] = self.registers[op_a] % self.registers[op_b]
        else:
            raise ValueError(f"Unknown opcode: {opcode}")

def check(simulator: Simulator, output_data: dict):
    if output_data["PC"] != simulator.pc:
        print(f"Mismatch at PC: expected {simulator.pc}, got {output_data['PC']}")
        return False

    # Check if the exception value is correct
    if output_data["Exception"] != simulator.exception:
        print(f"Mismatch at Exception: expected {simulator.exception}, got {output_data['Exception']}")
        return False

    # Check if the exception PC is correct
    if output_data["ExceptionPC"] != simulator.exception_pc:
        print(f"Mismatch at ExceptionPC: expected {simulator.exception_pc}, got {output_data['ExceptionPC']}")
        return False

    # Check if all busybits are false
    for i in range(num_registers):
        if output_data["BusyBitTable"][i] != 0:
            print(f"Mismatch at BusyBitTable: expected 0, got {output_data['BusyBitTable'][i]}")
            return False

    for logical_regi in range(num_registers):
        physical_regi = output_data["RegisterMapTable"][logical_regi]
        output_value = output_data["PhysicalRegisterFile"][physical_regi]

        if simulator.registers[f"x{logical_regi}"] != output_value:
            print(f"Mismatch at register x{logical_regi}: expected {simulator.registers[f'x{logical_regi}']}, got {output_value}")
            return False

    return True

def main():
    parser = argparse.ArgumentParser(description="Test script to test the simulator")
    parser.add_argument('input', type=str, help="Input json path")
    parser.add_argument('output', type=str, help="Output json path")
    parser.add_argument('--binary', type=str, default="build/simulate",
                        help='Path to the binary to run')
    args = parser.parse_args()

    # Read the input JSON file
    with open(args.input, 'r') as f:
        input_data = json.load(f)

    simulator = Simulator()

    for instruction in input_data:
        simulator.step(instruction)

    # Run the simulator
    subprocess.run([args.binary, args.input, args.output])

    # Read the output JSON file
    with open(args.output, 'r') as f:
        output_data = json.load(f)

    # Check the output
    if check(simulator, output_data[-1]):
        print("All checks passed.")
    else:
        print("Some checks failed.")

if __name__ == "__main__":
    main()