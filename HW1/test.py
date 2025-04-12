import argparse
import numpy as np
import json
import pathlib
import random
import subprocess

num_registers = 32
physical_registers = 64

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

    # Check if the active list is empty
    if len(output_data["ActiveList"]) != 0:
        print(f"Mismatch at ActiveList: expected empty, got {output_data['ActiveList']}")
        return False

    # Check if all busybits are false
    for i in range(num_registers):
        if output_data["BusyBitTable"][i] != 0:
            print(f"Mismatch at BusyBitTable: expected 0, got {output_data['BusyBitTable'][i]}")
            return False

    # Check if the decoded PCs are empty
    if len(output_data["DecodedPCs"]) != 0:
        print(f"Mismatch at DecodedPCs: expected empty, got {output_data['DecodedPCs']}")
        return False

    # Check if the FreeList and RegisterMapTable contains all registers
    if len(output_data["FreeList"]) != num_registers:
        print(f"Mismatch at FreeList: expected {num_registers}, got {len(output_data['FreeList'])}")
        return False

    registers = set(output_data["RegisterMapTable"] + output_data["FreeList"])
    if len(registers) != physical_registers:
        print(f"Not all physical registers are used: expected {physical_registers}, got {len(registers)}")
        return False

    # Check if the integer queue is empty
    if len(output_data["IntegerQueue"]) != 0:
        print(f"Mismatch at IntegerQueue: expected empty, got {output_data['IntegerQueue']}")
        return False

    # Check if the logical registers have correct values
    for logical_regi in range(num_registers):
        physical_regi = output_data["RegisterMapTable"][logical_regi]
        output_value = output_data["PhysicalRegisterFile"][physical_regi]

        if simulator.registers[f"x{logical_regi}"] != output_value:
            print(f"Mismatch at register x{logical_regi}: expected {simulator.registers[f'x{logical_regi}']}, got {output_value}")
            return False

    return True

def generate_program(max_instructions=100):
    """
    Generate a random program with a given number of instructions.
    :param max_instructions: Maximum number of instructions to generate.
    :return: A list of instructions.
    """
    instructions = []
    for _ in range(random.randint(1, max_instructions)):
        opcode = random.choice(["add", "addi", "sub", "mulu", "divu", "remu"])
        dest = f"x{random.randint(0, num_registers - 1)}"
        op_a = f"x{random.randint(0, num_registers - 1)}"
        op_b = f"x{random.randint(0, num_registers - 1)}"
        if opcode == "addi":
            op_b = np.int64(random.randint(-2 ** 63, 2 ** 63 - 1))
        instructions.append(f"{opcode} {dest}, {op_a}, {op_b}")
    return instructions

def fuzz_test():
    """
    Fuzz test the simulator by generating random programs and checking the output.
    """
    parser = argparse.ArgumentParser(description="Test script to test the simulator")
    parser.add_argument('--binary', type=str, default="build/simulate",
                        help='Path to the binary to run')
    parser.add_argument('--max_instructions', type=int, default=50,
                        help='Maximum number of instructions to generate')
    parser.add_argument('--num_tests', type=int, default=10,
                        help='Number of tests to run')
    args = parser.parse_args()

    tests_path = pathlib.Path('tests')

    for i in range(args.num_tests):
        # Generate a random program
        instructions = generate_program(max_instructions=args.max_instructions)

        # Write the instructions to a JSON file
        filename = f"test_{i:05d}.json"
        with open(tests_path / filename, 'w') as f:
            json.dump(instructions, f, indent=4)
        # # Read the instructions from a JSON file
        # filename = f"test_{i:05d}.json"
        # with open(tests_path / filename, 'r') as f:
        #     instructions = json.load(f)

        simulator = Simulator()

        for instruction in instructions:
            simulator.step(instruction)

        # Run the simulator
        output_filepath = tests_path / 'out' / filename
        subprocess.run([args.binary, tests_path / filename, output_filepath], stdout=subprocess.DEVNULL)

        # Read the output JSON file
        with open(output_filepath, 'r') as f:
            output_data = json.load(f)

        # Check the output
        if check(simulator, output_data[-1]):
            print(f"All checks for {i} passed.")
        else:
            print(f"Some checks for {i} failed.")

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
    # main()
    fuzz_test()