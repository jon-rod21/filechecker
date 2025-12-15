#!/bin/bash

# Define the output file name
OUTPUT_FILE="fcheck_test_results_validated.txt"

# Clear the output file before starting
> "$OUTPUT_FILE"

# Define the test cases and their expected outcomes (Success or Error message)
declare -A TEST_CASES=(
    ['addronce']='ERROR: direct address used more than once.'
    ['addronce2']='ERROR: indirect address used more than once.'
    ['badaddr']='ERROR: bad direct address in inode.'
    ['badfmt']='ERROR: directory not properly formatted.'
    ['badindir1']='ERROR: bad indirect address in inode.'
    ['badindir2']='ERROR: bad indirect address in inode.'
    ['badinode']='ERROR: bad inode.'
    ['badlarge']='ERROR: directory appears more than once in file system.'
    ['badrefcnt']='ERROR: bad reference count for file.'
    ['badrefcnt2']='ERROR: bad reference count for file.'
    ['badroot']='ERROR: root directory does not exist.'
    ['badroot2']='ERROR: root directory does not exist.'
    ['dironce']='ERROR: directory appears more than once in file system.'
    ['good']='SUCCESS: File system is clean.' # Assuming a successful run prints no error/warning
    ['goodlarge']='SUCCESS: File system is clean.'
    ['goodlink']='SUCCESS: File system is clean.'
    ['goodrefcnt']='SUCCESS: File system is clean.'
    ['goodrm']='SUCCESS: File system is clean.'
    ['imrkfree']='ERROR: inode referred to in directory but marked free.'
    ['imrkused']='ERROR: inode marked use but not found in a directory.'
    ['indirfree']='ERROR: address used by inode but marked free in bitmap.'
    ['mismatch']='ERROR: directory not properly formatted.' # Expected error based on your note
    ['mrkfree']='ERROR: address used by inode but marked free in bitmap.'
    ['mrkused']='ERROR: bitmap marks block in use but it is not in use.'
)

# Loop through each test case
for testcase in "${!TEST_CASES[@]}"; do
    expected_output=${TEST_CASES[$testcase]}
    TEST_PATH="testcases/$testcase"

    # --- Header for the current test case ---
    echo "==========================================================" >> "$OUTPUT_FILE"
    echo "Test Case: $testcase" >> "$OUTPUT_FILE"
    echo -e "Expected Result:\n$expected_output" >> "$OUTPUT_FILE"
    echo "----------------------------------------------------------" >> "$OUTPUT_FILE"

    # Run the fcheck command and append the output
    # Redirect both stdout and stderr (2>&1)
    ./fcheck "$TEST_PATH" >> "$OUTPUT_FILE" 2>&1
    
    # Add separators
    echo -e "\n\n" >> "$OUTPUT_FILE"
done

echo "✅ All tests completed. Results, including expected outcomes, are saved in $OUTPUT_FILE"
