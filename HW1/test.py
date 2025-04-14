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

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

class Simulator:
    def __init__(self):
        self.registers = {}
        for i in range(num_registers):
            self.registers[f"x{i}"] = np.uint64(0)
        self.exception = False
        self.exception_pc = 0
        self.pc = 0
        self.trace = [self.registers.copy()]

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
            with np.errstate(over="raise"):
                try:
                    self.registers[op_a] + self.registers[op_b]
                except FloatingPointError:
                    print(f"Overflow at PC {self.pc}: {instruction}")

            with np.errstate(over="ignore"):
                self.registers[dest] = self.registers[op_a] + self.registers[op_b]
        elif opcode == "addi":
            with np.errstate(over="raise"):
                try:
                    self.registers[op_a] + op_b.astype(np.uint64)
                except FloatingPointError:
                    print(f"Overflow at PC {self.pc}: {instruction}")

            with np.errstate(over="ignore"):
                self.registers[dest] = self.registers[op_a] + op_b.astype(np.uint64)
        elif opcode == "sub":
            with np.errstate(over="raise", under="raise"):
                try:
                    self.registers[op_a] - self.registers[op_b]
                except FloatingPointError:
                    print(f"Underflow at PC {self.pc}: {instruction}")

            with np.errstate(over="ignore", under="ignore"):
                self.registers[dest] = self.registers[op_a] - self.registers[op_b]
        elif opcode == "mulu":
            with np.errstate(over="raise"):
                try:
                    self.registers[op_a] * self.registers[op_b]
                except FloatingPointError:
                    print(f"Overflow at PC {self.pc}: {instruction}")

            with np.errstate(over="ignore"):
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

        # Update the trace
        self.trace.append(self.registers.copy())

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
    has_error = False
    for logical_regi in range(num_registers):
        physical_regi = output_data["RegisterMapTable"][logical_regi]
        output_value = output_data["PhysicalRegisterFile"][physical_regi]

        if simulator.registers[f"x{logical_regi}"] != output_value:
            has_error = True
            print(f"Mismatch at register x{logical_regi}: expected {simulator.registers[f'x{logical_regi}']}, got {output_value}")

    if has_error:
        return False

    return True

def generate_program(min_instructions=1, max_instructions=100, opcodes=None):
    """
    Generate a random program with a given number of instructions.
    :param opcodes: List of opcodes to use. If None, use all available opcodes.
    :param min_instructions: Minimum number of instructions to generate.
    :param max_instructions: Maximum number of instructions to generate.
    :return: A list of instructions.
    """
    if opcodes is None:
        opcodes = ["add", "addi", "sub", "mulu", "divu", "remu"]
    instructions = []
    for _ in range(random.randint(min_instructions, max_instructions)):
        opcode = random.choice(opcodes)
        dest = f"x{random.randint(0, num_registers - 1)}"
        op_a = f"x{random.randint(0, num_registers - 1)}"
        op_b = f"x{random.randint(0, num_registers - 1)}"
        if opcode == "addi":
            op_b = random.randint(-2 ** 63, 2 ** 63 - 1)
        instructions.append(f"{opcode} {dest}, {op_a}, {op_b}")
    return instructions

def fuzz_test(args: argparse.Namespace):
    """
    Fuzz test the simulator by generating random programs and checking the output.
    """
    (args.tests_path / 'out').mkdir(parents=True, exist_ok=True)

    for i in range(args.num_tests):
        # Generate a random program
        instructions = generate_program(
            opcodes=args.opcodes,
            min_instructions=args.min_instructions,
            max_instructions=args.max_instructions,
        )

        # Write the instructions to a JSON file
        filename = f"test_{i:05d}.json"
        with open(args.tests_path / filename, 'w') as f:
            json.dump(instructions, f, indent=4)

        simulator = Simulator()

        for instruction in instructions:
            simulator.step(instruction)

        # Run the simulator
        output_filepath = args.tests_path / 'out' / filename
        subprocess.run([args.binary, args.tests_path / filename, output_filepath], stdout=subprocess.DEVNULL)

        # Read the output JSON file
        with open(output_filepath, 'r') as f:
            output_data = json.load(f)

        # Check the output
        if check(simulator, output_data[-1]):
            print(f"{bcolors.OKGREEN}All checks for {i} passed.{bcolors.ENDC}")
        else:
            print(f"{bcolors.FAIL}Some checks for {i} failed.{bcolors.ENDC}")

def check_test(args: argparse.Namespace):
    """
    Check the output of the simulator against a reference output.
    """
    with open(args.input, 'r') as f:
        instructions = json.load(f)

    simulator = Simulator()

    for instruction in instructions:
        simulator.step(instruction)

    # Run the simulator
    subprocess.run([args.binary, args.input, args.output], stdout=subprocess.DEVNULL)

    # Read the output JSON file
    with open(args.output, 'r') as f:
        output_data = json.load(f)

    # Check the output
    if check(simulator, output_data[-1]):
        print(f"{bcolors.OKGREEN}All checks passed.{bcolors.ENDC}")
    else:
        print(f"{bcolors.FAIL}Some checks failed.{bcolors.ENDC}")

def trace(args: argparse.Namespace):
    """
    Print the trace of the simulator.
    """
    with open(args.input, 'r') as f:
        instructions = json.load(f)

    simulator = Simulator()

    for instruction in instructions:
        simulator.step(instruction)

    # Print the trace
    print("Trace:")
    for i, regs in enumerate(simulator.trace):
        if args.register is not None:
            print(f"Step {i}: {args.register}={regs[args.register]}")
        else:
            print(f"Step {i}: {regs}")

def compare(args: argparse.Namespace):
    """
    Compare the output of two simulators.
    :param args:
    :return:
    """
    (args.tests_path / 'out').mkdir(parents=True, exist_ok=True)
    (args.tests_path / 'ref_out').mkdir(parents=True, exist_ok=True)

    for i in range(args.num_tests):
        # Generate a random program
        instructions = generate_program(
            opcodes=args.opcodes,
            min_instructions=args.min_instructions,
            max_instructions=args.max_instructions,
        )

        # Write the instructions to a JSON file
        filename = f"test_{i:05d}.json"
        with open(args.tests_path / filename, 'w') as f:
            json.dump(instructions, f, indent=4)

        # Run our simulator
        output_filepath = args.tests_path / 'out' / filename
        subprocess.run([args.binary, args.tests_path / filename, output_filepath], stdout=subprocess.DEVNULL)

        # Run the reference simulator
        ref_output_filepath = args.tests_path / 'ref_out' / filename
        ref_cmd = args.ref_cmd.format(input=args.tests_path / filename, output=ref_output_filepath)
        subprocess.run(ref_cmd.split(), stdout=subprocess.DEVNULL)

        # Run the compare script
        print(f"Comparing {output_filepath} and {ref_output_filepath}")
        subprocess.run(["python3", "compare.py", output_filepath, '-r', ref_output_filepath])

def main():
    parser = argparse.ArgumentParser(description="Test script to test the simulator")
    parser.add_argument('action', type=str, choices=['fuzz', 'check', 'compare', 'trace'],)
    parser.add_argument('--binary', type=str, default="build/simulate",
                        help='Path to the binary to run')
    parser.add_argument('--ref_cmd', type=str, default="build/simulate {input} {output}",
                        help='Command to run the reference binary, with {input} and {output} placeholders')
    parser.add_argument('--min_instructions', type=int, default=1,
                        help='Minimum number of instructions to generate')
    parser.add_argument('--max_instructions', type=int, default=50,
                        help='Maximum number of instructions to generate')
    parser.add_argument('--num_tests', type=int, default=10,
                        help='Number of tests to run')
    parser.add_argument('--opcodes', type=str, nargs='+', default=["add", "addi", "sub", "mulu", "divu", "remu"],)
    parser.add_argument('--input', type=str,
                        help='Path to the input JSON file to check')
    parser.add_argument('--output', type=str,
                        help='Path to the output JSON file to check')
    parser.add_argument('--register', type=str,
                        help='Register to check')
    parser.add_argument('--tests_path', type=str, default='tests',
                        help='Path to the tests directory')
    args = parser.parse_args()

    args.tests_path = pathlib.Path(args.tests_path)
    args.tests_path.mkdir(parents=True, exist_ok=True)

    if args.action == 'fuzz':
        fuzz_test(args)
    elif args.action == 'check':
        check_test(args)
    elif args.action == 'compare':
        compare(args)
    elif args.action == 'trace':
        trace(args)
    else:
        raise ValueError(f"Unknown action: {args.action}")

if __name__ == "__main__":
    main()