#include "DarkAppearance.h"
#import <AppKit/AppKit.h>

void forceDarkAppearance()
{
    if (@available (macOS 10.14, *))
        [NSApp setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua]];
}
