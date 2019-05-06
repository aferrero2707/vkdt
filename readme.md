
like:
- job manager
- some iops
- noise profiling (but maybe auto?)
- modularity

dont like:
- bloat
- cmake
- gtk
- lab
- dead features
- multi module pipeline
- speed

arch:
- vulkan / glsl compute shaders
  - this is not only about vs. opencl but also completely scheduling things on GPU,
    interleaving jobs right, and overall getting more performance for small jobs
    (thumbnail creation..)
- imgui
- does that mean c++ in the core?
- python bindings (pybind11)

dependencies:
- vulkan
- sdl2
- submodule imgui
- submodule rawspeed
- submodule pybind11
- libjpeg

image list:
- on-device thumbnail cache
- write back on shutdown?
- render as vertex list (see vkpt stretch_pic for text rendering)

pipeline:
- match pixels (dense correspondences, multi image)
  - highlight recon
  - denoise
- wavelet blend mode (separable a trous? even decimated a trous?)
  - heal/clone/blur
  - denoise (try multi-scale trick?)
- local laplacian
  - tone manipulation in low freqs
  - may allow us to use less octaves
- do we need local edits/masks
  - transform list of points interface
  - rasterise some shapes
- output colour management on display/export

pipeline implementation:
- two pipeline setups:
  - full (low res) image for thumbnail
  - one lowres preview and one cropped
    both setup with fixed size buffers, for interactive edit or tiled export
- fixed buffer allocs for all images
- number of pipelines for parallel GPU scheduling
- don't run preview if full is full roi
- see vkrun compute shaders

pipeline interface:
- "human readable" 64-bit tokens to identify module/param?
- get/set via generic interface
  - very simple serialisation as binary and "human readable"
- implement cli and vkrun wrapper around shaders/pipeline
- gui separate in a c++ compiled thing that calls into imgui
  - auto generate from params + meta description (curve etc)

iop:
- crop/rotate
- colour lut (but go from camera rgb directly?)
- tone curve
- local contrast
- sharpen
- demosaic
- highlights
- spot heal

camera specific data:
- require target shot/spectral calibration for input colour
- white balance presets
- fallback matrices?
- black and white?

metadata:
- probably no way around exiv2
- something with more comprehensive and simple interface
- editable?
- searchable?

db:
- special case to scale up to 1M images, can put everything
  into memory at all times (and write out at the end)
  - 4MB 32-bit indices of all images
  - 4MB sorted indices for list of stars
  - 5*4MB lists of colour labels
  - 30MB avg 30chars file names
- build bvh on demand for 3 dimensions sort query should be a matter of milliseconds
- rebuild after batch insertion of full directory
- built-in undo lists
- transactional interface with tokens similar to pipeline/iop
