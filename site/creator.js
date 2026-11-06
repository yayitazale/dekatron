// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Makes the animations of a theme from files of the user: an animated GIF, WebP or PNG, a video, or a
// numbered series of images. Each is fitted to the strip that crosses the six tubes, shrunk to the colors
// that the tubes can show, and stored as a scene, with a light for each tube if asked.
const DekatronCreator = (() => {
  const S = DekatronScene;
  const idle = () => new Promise((resolve) => setTimeout(resolve, 0));
  const TYPES = { gif: "image/gif", webp: "image/webp", png: "image/png", apng: "image/apng", jpg: "image/jpeg", jpeg: "image/jpeg" };
  const extension = (file) => (file.name.match(/\.([^.]+)$/) || [])[1]?.toLowerCase() || "";

  // The pictures of one animation, whatever file they come from. count(fps) says how many frames there are
  // at that rate, and frame(k, fps) gives the k-th as something that can be drawn.
  async function openSource(files) {
    const list = [...files].sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
    const first = list[0];
    if (list.length > 1) {
      return {
        still: false,
        count: () => list.length,
        async frame(k) {
          const bitmap = await createImageBitmap(list[k]);
          return { drawable: bitmap, width: bitmap.width, height: bitmap.height, release: () => bitmap.close() };
        },
        close() {},
      };
    }
    if (first.type.startsWith("video/")) return openVideo(first);
    if (typeof ImageDecoder === "undefined") throw new Error("This browser cannot read animated images. Use a video file, or a numbered series of images");
    const type = first.type || TYPES[extension(first)];
    if (!type) throw new Error(`${first.name}: not a picture or a video`);
    const decoder = new ImageDecoder({ data: await first.arrayBuffer(), type });
    await decoder.tracks.ready;
    const frames = decoder.tracks.selectedTrack.frameCount;
    const starts = [];
    let total = 0;
    for (let index = 0; index < frames; index++) {
      const { image } = await decoder.decode({ frameIndex: index });
      starts.push(total);
      total += (image.duration || 100000) / 1e6; // GIFs without a time play at 10 per second
      image.close();
    }
    return {
      still: frames === 1,
      count: (fps) => Math.max(1, Math.floor(total * fps)),
      async frame(k, fps) {
        const time = k / fps;
        let index = 0;
        while (index + 1 < frames && starts[index + 1] <= time) index++;
        const { image } = await decoder.decode({ frameIndex: index });
        return { drawable: image, width: image.displayWidth, height: image.displayHeight, release: () => image.close() };
      },
      close: () => decoder.close(),
    };
  }

  async function openVideo(file) {
    const video = document.createElement("video");
    video.muted = true;
    video.playsInline = true;
    video.preload = "auto";
    const link = URL.createObjectURL(file);
    video.src = link;
    await new Promise((resolve, reject) => {
      video.onloadedmetadata = resolve;
      video.onerror = () => reject(new Error(`${file.name}: this browser cannot play it`));
    });
    return {
      still: false,
      count: (fps) => Math.max(1, Math.floor(video.duration * fps)),
      async frame(k, fps) {
        await new Promise((resolve) => {
          video.onseeked = resolve;
          video.currentTime = Math.min(k / fps, Math.max(0, video.duration - 0.001));
        });
        return { drawable: video, width: video.videoWidth, height: video.videoHeight, release() {} };
      },
      close: () => URL.revokeObjectURL(link),
    };
  }

  // Draws a picture on the black strip: "fill" covers all of it and crops what sticks out, "fit" shows all of
  // the picture and leaves black around it.
  function painter(fit) {
    const canvas = document.createElement("canvas");
    canvas.width = S.CANVAS_WIDTH;
    canvas.height = S.HEIGHT;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    return ({ drawable, width, height }) => {
      context.fillStyle = "#000";
      context.fillRect(0, 0, canvas.width, canvas.height);
      const scale = (fit ? Math.min : Math.max)(canvas.width / width, canvas.height / height);
      context.drawImage(drawable, (canvas.width - width * scale) / 2, (canvas.height - height * scale) / 2, width * scale, height * scale);
      return context.getImageData(0, 0, canvas.width, canvas.height);
    };
  }

  // Every frame of a role, as strips: those asked for in `only` (all when it is left out).
  // How many frames a role has: the whole of the file, or the seconds asked for if that is less. A still
  // picture lasts as long as asked.
  const framesOf = (source, role) => {
    const asked = Math.max(1, Math.floor(role.seconds * role.fps));
    return Math.min(source.still ? asked : source.count(role.fps), asked, 900);
  };

  async function* strips(source, role, only) {
    const draw = painter(role.fit);
    const total = framesOf(source, role);
    for (let k = 0; k < total; k++) {
      if (only && !only.has(k)) continue;
      const picture = await source.frame(source.still ? 0 : k, role.fps);
      const strip = draw(picture);
      picture.release();
      yield [k, total, strip];
    }
  }

  const half = (strip) => {
    const width = strip.width >> 1;
    const height = strip.height >> 1;
    const data = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) data.set(strip.data.subarray(((y * 2) * strip.width + x * 2) * 4, ((y * 2) * strip.width + x * 2) * 4 + 4), (y * width + x) * 4);
    }
    return { data, width, height };
  };

  // The frames of one animation to look at, and the lights that its picture asks for. options has `gain` (how
  // strong the lights are, 1 is as the picture asks), `smoothing` (0 to 1, how quickly they follow, higher is
  // quicker) and `still` (true to follow only what moves over the first picture). Gives {fps, total, frames, auto},
  // where frames are half the size of the strip, and auto is 18 bytes of lights for each frame.
  async function previewRole(role, options, report, stop = {}) {
    const source = await openSource(role.files);
    try {
      const total = framesOf(source, role);
      const frames = [];
      const auto = new Uint8Array(total * 18);
      let follow = null;
      for await (const [k, , strip] of strips(source, role)) {
        if (stop.now) throw new Error("Stopped");
        if (k === 0) {
          const background = options.still ? { data: Uint8ClampedArray.from(strip.data) } : null;
          follow = S.ledFollower(background, 0.12, options.smoothing);
        }
        auto.set(follow(strip), k * 18);
        frames.push(half(strip));
        if (k % 3 === 0) {
          report(`${role.label}: frame ${k + 1} of ${total}`, (k + 1) / total);
          await idle();
        }
      }
      return { fps: role.fps, total, frames, auto: options.gain === 1 ? auto : DekatronLights.scale(auto, options.gain) };
    } finally {
      source.close();
    }
  }

  // roles: [{ name, files, fps, seconds, fit, lights, leds }] for the animations that are new. `leds` are lights
  // that are already made (from previewRole and the flashes of the user), 18 bytes for each frame, and are used
  // in place of the lights that follow the picture. Gives
  // { clips: [entries], bytes } and calls report(text, fraction) as it goes. `stop.now` ends it early.
  async function makeClips(roles, capacity, report, stop = {}) {
    const sources = [];
    for (const role of roles) sources.push(await openSource(role.files));
    try {
      // The palette is shared by the animations, so they come out in the same colors and can share images.
      const samples = [];
      for (const [index, role] of roles.entries()) {
        const total = framesOf(sources[index], role);
        const wanted = new Set(Array.from({ length: Math.min(8, total) }, (_, n) => Math.floor((n * total) / Math.min(8, total))));
        for await (const [, , strip] of strips(sources[index], role, wanted)) samples.push(half(strip));
        report(`Choosing colors for ${role.label}`, (index + 1) / (roles.length * 4));
        await idle();
      }
      const entries = S.buildPalette(samples);
      const quantize = S.makeQuantizer(entries);
      const clips = [];
      let bytes = 0;
      for (const [index, role] of roles.entries()) {
        const encoder = S.sceneEncoder(entries);
        const follow = role.lights && !role.leds ? S.ledFollower() : null;
        const lights = [];
        for await (const [k, total, strip] of strips(sources[index], role)) {
          if (stop.now) throw new Error("Stopped");
          encoder.add(quantize(strip));
          if (follow) lights.push(follow(strip));
          if (k % 3 === 0) {
            report(`${role.label}: frame ${k + 1} of ${total}`, 0.25 + 0.75 * ((index + (k + 1) / total) / roles.length));
            await idle();
          }
          if (bytes + encoder.size() > capacity) throw new Error(`${role.label} is too big for the clock. Try fewer seconds, fewer frames per second, or something with less moving`);
        }
        let leds = follow ? new Uint8Array(lights.length * 18) : null;
        if (follow) lights.forEach((frame, i) => leds.set(frame, i * 18));
        if (role.leds) {
          if (role.leds.length !== framesOf(sources[index], role) * 18) throw new Error(`The lights of ${role.label} do not fit its frames. Press Preview again`);
          leds = role.leds;
        }
        const scene = encoder.finish(leds);
        bytes += encoder.size() + (leds ? leds.length : 0);
        clips.push(S.toClip(role.name, role.fps, scene));
      }
      return { clips, bytes };
    } finally {
      sources.forEach((source) => source.close());
    }
  }

  return { makeClips, previewRole };
})();
