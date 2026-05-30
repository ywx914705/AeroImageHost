<template>
  <aside class="sidebar">
    <div class="logo">
      <div class="logo-box"><i data-lucide="cloud"></i></div>
      <div>
        <h1>AeroImageHost</h1>
        <p>高性能图床平台</p>
      </div>
    </div>
    <div class="workspace">
      <div class="workspace-top">
        <div>
          <div class="workspace-label">工作空间</div>
          <div class="workspace-name">{{ appStore.username }}</div>
        </div>
      </div>
    </div>
    <nav class="nav">
      <router-link v-for="item in navItems" :key="item.path" :to="item.path" class="nav-item" active-class="active">
        <i :class="item.icon"></i> {{ item.label }}
      </router-link>
    </nav>
    <div class="storage-card">
      <div class="label">存储空间</div>
      <div class="value">{{ storageText }}</div>
      <div class="bar"><div class="bar-fill" :style="{width: '30%'}"></div></div>
    </div>
    <div class="user-card">
      <div class="user-avatar">{{ appStore.username?.charAt(0)?.toUpperCase() || 'U' }}</div>
      <div class="user-info">
        <h4>{{ appStore.username }}</h4>
        <p>{{ filesStore.total }} 个文件</p>
      </div>
      <button class="user-logout" @click="handleLogout"><i class="fas fa-sign-out-alt"></i></button>
    </div>
  </aside>
</template>

<script setup>
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { useFilesStore } from '../stores/files'

const router = useRouter()
const appStore = useAppStore()
const filesStore = useFilesStore()

const navItems = [
  { path: '/dashboard', label: '控制台', icon: 'fas fa-th-large' },
  { path: '/upload', label: '上传中心', icon: 'fas fa-cloud-upload-alt' },
  { path: '/library', label: '我的图床', icon: 'fas fa-folder' },
  { path: '/hall', label: '图床大厅', icon: 'fas fa-images' },
]

const storageText = computed(() => {
  const bytes = filesStore.totalSize || 0
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB'
  if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + ' MB'
  return (bytes / 1073741824).toFixed(2) + ' GB'
})

function handleLogout() {
  appStore.logout()
  router.push('/')
}
</script>
