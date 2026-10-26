# Third-party source and distribution notes

This project uses JUCE 8.0.12, commit 29396c22c93392d6738e021b83196283d6e4d850. The DSP in Source/DSP.h is project source, not an extracted or copied commercial vocal processor. No artist voices, recordings or samples are included.

JUCE modules are offered under AGPLv3 and commercial JUCE licensing. The build fetches JUCE rather than including its entire source tree. Its license and dependency notices are retained in the JUCE checkout. Before distributing the combined product, choose and satisfy the applicable JUCE license. This prototype does not include a purchased commercial JUCE license or make that choice for the owner.

- JUCE source and license: https://github.com/juce-framework/JUCE/tree/8.0.12
- JUCE license text: https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md
- JUCE AU wrapper dependency AudioUnitSDK: Apache 2.0.
- JUCE VST3 SDK dependency: MIT, as recorded in its pinned checkout.
- JUCE also contains font/graphics and audio codec dependencies. Consult the pinned JUCE LICENSE.md and module notices for their complete terms.

The application/plugin builds are for local development and evaluation. Public distribution, final branding and code signing are not completed in this build.
