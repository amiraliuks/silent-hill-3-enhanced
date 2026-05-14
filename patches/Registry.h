#pragma once

// ============================================================
//  Registry compatiblity fixes
// ============================================================

namespace Patches
{
    // Creates the registry keys and paths SH3 expects
    // Called directly during process attach
    void WriteRegistryKeysNow();
    
    // Installs registry redirect hooks
    void ApplyRegistryRedirect();
}