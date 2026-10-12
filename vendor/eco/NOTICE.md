# Third-party Eco framework files

The unmodified SDK headers and Linux x86_64 static libraries in this directory
come from the official `peerf-eco/eco-harness-releases` release **v0.1.3**, asset
`marketplace_index.zip`:

https://github.com/peerf-eco/eco-harness-releases/releases/tag/v0.1.3

`provenance.json` records the source URL, archive SHA256, original archive member
paths, individual file hashes and component versions. Only the eleven headers
and four runtime libraries needed by this project are retained.

Copyright notices in upstream files are preserved verbatim, including
`Copyright (c) ... Vladimir Bashev. All rights reserved.` EcoFFT does not
relicense these files or claim authorship of them. See the upstream provider
for the terms applicable to the Eco SDK.

The FFT implementation in `src/` is separate from the SDK. Neither component
library links an external FFT implementation. The optional comparison client
links system-provided FFTW; it is not part of this SDK or the component.
