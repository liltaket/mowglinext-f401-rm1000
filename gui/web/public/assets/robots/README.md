# Map robot images

Store bundled, display-only robot images in a directory named for the external
product identity, for example `biltema-rm1000/mower.webp` and later
`biltema-rm1000/dock.webp`. Hardware preset names are not proof of shell
identity; add an appearance mapping only when that identity is explicit.

Prepare each image as a square 512 × 512 transparent PNG or WebP. Use a true
top-down view with the robot's front pointing to the top edge. Crop around the
visible object, leave a small even margin, and center it. Preserve transparent
pixels. Keep the asset crisp; lossless WebP or PNG is preferred for detailed
artwork.

The mower appearance metadata records the fraction of the square occupied by the
visible body length, its represented physical length, and the normalized pose
anchor (`0..1` from left/top to right/bottom). The RM1000's 0.57 m overall length
is reported by [Forbrugerrådet Tænk's product test](https://taenk.dk/test/robotplaeneklipper/biltema-rm1000).
Its front points to the image's top edge. The configured rear-axle anchor is an
image-based estimate; confirm it against a model-specific `base_link` measurement
before treating sub-body map alignment as calibrated.

The RM1000 mower image comes from a photograph taken by the contributor and
edited with AI-assisted tools. The contributor controls the source-image rights
and intentionally contributes the resulting asset under this repository's
licensing terms. Add the same provenance note for a dock image when a
contributor-supplied dock photograph is available; there is no dock image in the
repository yet. Do not estimate a dock image's physical dimensions or pose anchor
from the generic map-server keepout dimensions.

Dock assets should use an explicit normalized pose anchor and physical scale.
Keep the existing drawn footprint and dock marker as runtime fallbacks whenever
an image or valid pose is missing or an image cannot be decoded.
