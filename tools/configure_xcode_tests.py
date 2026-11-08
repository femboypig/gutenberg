#!/usr/bin/env python3
from copy import deepcopy
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

project = Path(sys.argv[1])
scheme = project / "xcshareddata" / "xcschemes" / "GutenbergTests.xcscheme"
tree = ET.parse(scheme)
root = tree.getroot()
reference = next(
    item for item in root.findall("./BuildAction/BuildActionEntries/BuildActionEntry/BuildableReference")
    if item.attrib.get("BlueprintName") == "GutenbergTests"
)
action = root.find("TestAction")
if action is None:
    action = ET.SubElement(root, "TestAction", {
        "buildConfiguration": "Debug",
        "selectedDebuggerIdentifier": "Xcode.DebuggerFoundation.Debugger.LLDB",
        "selectedLauncherIdentifier": "Xcode.IDEFoundation.Launcher.LLDB",
        "shouldUseLaunchSchemeArgsEnv": "YES",
    })
testables = action.find("Testables")
if testables is None:
    testables = ET.SubElement(action, "Testables")
testables.clear()
testable = ET.SubElement(testables, "TestableReference", {"skipped": "NO"})
testable.append(deepcopy(reference))
ET.indent(tree, space="   ")
tree.write(scheme, encoding="UTF-8", xml_declaration=True)
