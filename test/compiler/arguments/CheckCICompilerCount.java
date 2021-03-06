/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

import jdk.test.lib.*;

/*
 * @test CheckCheckCICompilerCount
 * @bug 8130858
 * @summary Check that correct range of values for CICompilerCount are allowed depending on whether tiered is enabled or not
 * @library /testlibrary
 * @modules java.base/sun.misc
 *          java.management
 * @run main CheckCICompilerCount
 */

public class CheckCICompilerCount {
    private static final String[][] NON_TIERED_ARGUMENTS = {
        {
            "-XX:-TieredCompilation",
            "-XX:+PrintFlagsFinal",
            "-XX:CICompilerCount=0",
            "-version"
        },
        {
            "-XX:-TieredCompilation",
            "-XX:+PrintFlagsFinal",
            "-XX:CICompilerCount=1",
            "-version"
        }
    };

    private static final String[][] NON_TIERED_EXPECTED_OUTPUTS = {
        {
            "CICompilerCount=0 must be at least 1",
            "Improperly specified VM option 'CICompilerCount=0'"
        },
        {
            "intx CICompilerCount                          := 1                                   {product}"
        }
    };

    private static final int[] NON_TIERED_EXIT = {
        1,
        0
    };

    private static final String[][] TIERED_ARGUMENTS = {
        {
            "-XX:+TieredCompilation",
            "-XX:+PrintFlagsFinal",
            "-XX:CICompilerCount=1",
            "-version"
        },
        {
            "-XX:+TieredCompilation",
            "-XX:+PrintFlagsFinal",
            "-XX:CICompilerCount=2",
            "-version"
        }
    };

    private static final String[][] TIERED_EXPECTED_OUTPUTS = {
        {
            "CICompilerCount=1 must be at least 2",
            "Improperly specified VM option 'CICompilerCount=1'"
        },
        {
            "intx CICompilerCount                          := 2                                   {product}"
        }
    };

    private static final int[] TIERED_EXIT = {
        1,
        0
    };

    private static void verifyValidOption(String[] arguments, String[] expected_outputs, int exit, boolean tiered) throws Exception {
        ProcessBuilder pb;
        OutputAnalyzer out;

        pb = ProcessTools.createJavaProcessBuilder(arguments);
        out = new OutputAnalyzer(pb.start());

        try {
            out.shouldHaveExitValue(exit);
            for (String expected_output : expected_outputs) {
                out.shouldContain(expected_output);
            }
        } catch (RuntimeException e) {
            // Check if tiered compilation is available in this JVM
            // Version. Throw exception only if it is available.
            if (!(tiered && out.getOutput().contains("TieredCompilation is disabled in this release."))) {
                throw new RuntimeException(e);
            }
        }
    }

    public static void main(String[] args) throws Exception {
        if (NON_TIERED_ARGUMENTS.length != NON_TIERED_EXPECTED_OUTPUTS.length || NON_TIERED_ARGUMENTS.length != NON_TIERED_EXIT.length) {
            throw new RuntimeException("Test is set up incorrectly: length of arguments, expected outputs and exit codes in non-tiered mode of operation do not match.");
        }

        if (TIERED_ARGUMENTS.length != TIERED_EXPECTED_OUTPUTS.length || TIERED_ARGUMENTS.length != TIERED_EXIT.length) {
            throw new RuntimeException("Test is set up incorrectly: length of arguments, expected outputs and exit codes in tiered mode of operation do not match.");
        }

        for (int i = 0; i < NON_TIERED_ARGUMENTS.length; i++) {
            verifyValidOption(NON_TIERED_ARGUMENTS[i], NON_TIERED_EXPECTED_OUTPUTS[i], NON_TIERED_EXIT[i], false);
        }

        for (int i = 0; i < TIERED_ARGUMENTS.length; i++) {
            verifyValidOption(TIERED_ARGUMENTS[i], TIERED_EXPECTED_OUTPUTS[i], TIERED_EXIT[i], true);
        }
    }
}
