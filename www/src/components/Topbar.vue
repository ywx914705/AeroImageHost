<template>
  <header class="topbar">
    <div class="top-left">
      <h2>{{ title }}</h2>
      <p>{{ subtitle }}</p>
    </div>
    <div class="top-right">
      <div class="search-box">
        <i class="fas fa-search" style="color:#9ca3af;font-size:14px"></i>
        <input :placeholder="searchPlaceholder" v-model="searchModel" @keyup.enter="$emit('search')" />
      </div>
      <button class="top-btn" @click="appStore.toggleTheme">
        <i :class="appStore.isDark ? 'fas fa-sun' : 'fas fa-moon'"></i>
      </button>
    </div>
  </header>
</template>

<script setup>
import { useAppStore } from '../stores/app'

const props = defineProps({
  title: { type: String, default: '' },
  subtitle: { type: String, default: '' },
  searchPlaceholder: { type: String, default: '搜索文件...' },
  searchModel: { type: String, default: '' },
})

defineEmits(['search'])

const appStore = useAppStore()
</script>
