// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <JuceHeader.h>

namespace studio
{
class CalibrationPanel final : public juce::Component
{
public:
    CalibrationPanel();
    ~CalibrationPanel() override;
    void resized() override;
private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
