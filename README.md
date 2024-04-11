# Phase 4a

-- Testcase 01 should be passing - all processes have similar expected vs actual wake up times and all have same priority

-- Testcase 02 should be passing - the timing for each process after sleeping for its designated amount of time is slightly off and the expected pid for each process differs from the actual pid which is ok

-- Testcase 07 should be passing - each child starts and terminates in their respective order with small variations in the timing of each character being written since each child writes to its own terminal which is indepedent of the writing of other children to other terminals 

-- Testcase 20 should be passing - the order of when Child 21 writes to term1 and when Child 0 reads is flipped but this is okay since reading and writing is independent. This is also the case for Child 23 writing and Child 2 reading.

-- Testcase 22 should be passing - the order of when "buffer written 'two: second line" and  "buffer written 'one: third line, longer than previous ones" is flipped by this is okay because both children are writing to different terminals 










