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

The mower appearance metadata records the fraction of the square occupied by
the visible body length, its represented physical length, and the normalized
vertical location of `base_link` (`0` at the top, `1` at the bottom). `base_link`
must align with the live ROS pose; do not center the image on the body's visual
center unless that is where the pose frame is. Dock assets should use an
equivalent explicit anchor convention when dock imagery is added. Keep drawn
footprints and markers as the runtime fallback whenever an image is missing or
cannot be decoded.
