#!/bin/bash

# Define the output file name
OUTPUT_FILE="fcheck_test_results.txt"

# Clear the output file before starting (optional: use >> to append)
> "$OUTPUT_FILE"

# List of test case names
TEST_CASES=(
    'addronce'
    'addronce2'
    'badaddr'
    'badfmt'
    'badindir1'
    'badindir2'
    'badinode'
    'badlarge'
    'badrefcnt'
    'badrefcnt2'
    'badroot'
    'badroot2'
    'dironce'
    'good'
    'goodlarge'
    'goodlink'
    'goodrefcnt'
    'goodrm'
    'imrkfree'
    'imrkused'
    'indirfree'
    'mismatch'
    'mrkfree'
    'mrkused'
)

# Loop through each test case
for testcase in "${TEST_CASES[@]}"; do
    # Print a header for the current test case to the output file
    echo "==========================================================" >> "$OUTPUT_FILE"
    echo "Running Test Case: $testcase" >> "$OUTPUT_FILE"
    echo "==========================================================" >> "$OUTPUT_FILE"

    # Construct the full path
    TEST_PATH="testcases/$testcase"

    # Run the fcheck command and append both standard output and standard error (2>&1)
    # to the output file.
    ./fcheck "$TEST_PATH" >> "$OUTPUT_FILE" 2>&1
    
    # Add a separator
    echo -e "\n\n" >> "$OUTPUT_FILE"
done

echo "✅ All tests completed. Results are saved in $OUTPUT_FILE"
