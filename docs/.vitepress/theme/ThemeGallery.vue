<script setup lang="ts">
import { onMounted, ref } from 'vue';

// The gallery is index.json of the themes that the release publishes next to the site (tools/build_themes.py).
declare const __DOCS_BASE__: string;

interface Theme { id: string; name: string; author: string; license: string; description: string; file: string; preview: string; bytes: number }

const themes = ref<Theme[]>([]);
const state = ref<'loading' | 'ready' | 'empty'>('loading');
const base = `${__DOCS_BASE__}themes/`;

onMounted(async () => {
  try {
    const response = await fetch(`${base}index.json`, { cache: 'no-cache' });
    if (!response.ok) throw new Error(String(response.status));
    themes.value = (await response.json()).themes;
    state.value = themes.value.length ? 'ready' : 'empty';
  } catch {
    state.value = 'empty';
  }
});
</script>

<template>
  <p v-if="state === 'loading'">Loading the themes…</p>
  <p v-else-if="state === 'empty'">The themes are published with each release. There are none to download yet.</p>
  <div v-else class="gallery">
    <article v-for="theme in themes" :key="theme.id" class="theme-card">
      <img :src="base + theme.preview" :alt="`A preview of the ${theme.name} theme on the six tubes`" loading="lazy" />
      <div class="body">
        <div class="name">{{ theme.name }}</div>
        <div class="meta">
          {{ [theme.author && `by ${theme.author}`, theme.license, `${Math.round(theme.bytes / 1024)} KB`].filter(Boolean).join(' · ') }}
        </div>
        <p v-if="theme.description">{{ theme.description }}</p>
        <a class="download" :href="base + theme.file" :download="`${theme.id}.theme.bin`" target="_self">Download</a>
      </div>
    </article>
  </div>
</template>
