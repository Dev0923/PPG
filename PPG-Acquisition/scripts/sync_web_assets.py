with open(r'c:\Users\devsa\Desktop\Hardware\PPG-Acquisition\data\index.html', 'r', encoding='utf-8') as f:
    html = f.read()

header = """#pragma once

#include <Arduino.h>

/**
 * @brief Embedded fallback PROGMEM HTML for the research web dashboard.
 * Serves automatically if LittleFS filesystem image has not been uploaded yet.
 */
static const char INDEX_HTML_PROGMEM[] PROGMEM = R\"rawliteral("""

footer = """)rawliteral\";\n"""

with open(r'c:\Users\devsa\Desktop\Hardware\PPG-Acquisition\include\web_assets.h', 'w', encoding='utf-8') as f:
    f.write(header + html + footer)

print("web_assets.h updated successfully.")
