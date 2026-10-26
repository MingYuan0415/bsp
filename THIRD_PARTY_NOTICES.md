# Third-Party Component Notices

`XPowersLib/` is a vendored source snapshot of
[Lewis He's XPowersLib](https://github.com/lewisxhe/XPowersLib), licensed under
MIT. The upstream version is declared as 0.3.4 and pinned at commit
`d6997586e68f65afd51baa775903df930db39821` (2026-07-01). This directory keeps
that commit's `src/` and `LICENSE`; examples, datasheets and CI files are not
included, no semantic patch has been applied to the upstream source, and line
endings were normalized to LF per this repository's rules when committed.

Do not modify code in that directory except for version upgrades or an
explicitly recorded upstream patch. Board adaptation, I2C callbacks, error
mapping and resource lifecycle all live in
`waveshare/esp32-s3-touch-amoled-1.8/mt_axp2101.cpp`.

The full MIT license text is in `XPowersLib/LICENSE`; distributing this
snapshot must also retain the copyright and license notices in its source
files.
