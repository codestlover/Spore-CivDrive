
#pragma once

namespace cfg {
struct Values {
    float speedMul = 1.0f;
    float turnRate = 1.7f;
    float mouseSens = 0.25f;
    int invertY = 0;
    float camDistance = 1.0f;
    float camHeight = 1.0f;
    int autoCenter = 1;
    int lockCursorOnOrbit = 1;
    float enterSeconds = 1.0f;
    float exitSeconds = 0.8f;
    int shader = 1;
    float shaderStrength = 1.0f;
    float pilotVignette = 0.22f;
    float doubleClickMs = 450.0f;
    float doubleShiftMs = 400.0f;
    int autonomousFire = 0;
    int verbose = 0;
};

const Values& Get();
}

