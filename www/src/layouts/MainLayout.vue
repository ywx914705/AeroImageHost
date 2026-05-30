<template>
  <div class="app-layout">
    <!-- 侧边栏 -->
    <aside class="sidebar">
      <div class="logo">
        <div class="logo-box"><i class="fas fa-cloud"></i></div>
        <div>
          <h1>AeroImageHost</h1>
          <p>高性能图床平台</p>
        </div>
      </div>

      <div class="workspace">
        <div class="workspace-top">
          <div>
            <div class="workspace-label">工作空间</div>
            <div class="workspace-name">{{ wsStore.currentWorkspace.name }}</div>
          </div>
          <button class="ws-add" @click="showNewWorkspace = true" title="新建工作空间">
            <i class="fas fa-plus"></i>
          </button>
        </div>
        <div class="workspace-list">
          <div v-for="ws in wsStore.workspaces" :key="ws.id" class="ws-item" :class="{ active: wsStore.currentWorkspace.id === ws.id }" @click="wsStore.switchWorkspace(ws)">
            <span>{{ ws.name }}</span>
            <button v-if="!ws.is_default" class="ws-delete" @click.stop="wsStore.deleteWorkspace(ws.id)" title="删除">
              <i class="fas fa-times"></i>
            </button>
          </div>
        </div>
      </div>

      <nav class="nav">
        <router-link v-for="item in navItems" :key="item.to" :to="item.to" class="nav-item" active-class="active">
          <i :class="item.icon"></i> {{ item.label }}
        </router-link>
      </nav>

      <FolderTree
        :tree="filesStore.folderTree"
        :current-folder-id="filesStore.currentFolderId"
        @select="selectFolder"
        @create="showCreateFolder"
        @rename="showRenameFolder"
        @delete="handleDeleteFolder"
      />

      <div class="storage-card">
        <div class="label">存储空间</div>
        <div class="value">{{ formatSize(stats.total_size || 0) }}</div>
        <div class="bar"><div class="bar-fill" :style="{width: Math.min(100, (stats.total_files || 0) / 100 * 100) + '%'}"></div></div>
      </div>

      <div class="user-card">
        <div class="user-avatar">{{ appStore.username ? appStore.username.charAt(0).toUpperCase() : 'U' }}</div>
        <div class="user-info">
          <h4>{{ appStore.username }}</h4>
          <p>{{ stats.total_files || 0 }} 个文件</p>
        </div>
        <button class="user-logout" @click="handleLogout" title="退出"><i class="fas fa-sign-out-alt"></i></button>
      </div>
    </aside>

    <!-- 主内容区 -->
    <div class="main-content">
      <div class="topbar">
        <div class="top-left">
          <h2>{{ currentTitle }}</h2>
          <p>现代化媒体资产管理平台</p>
        </div>
        <div class="top-right">
          <div class="search-box">
            <i class="fas fa-search" style="color:#9ca3af"></i>
            <input placeholder="搜索文件..." v-model="filesStore.search" @keyup.enter="filesStore.loadFiles()" />
          </div>
          <button class="top-btn" @click="appStore.toggleTheme()"><i :class="appStore.theme === 'dark' ? 'fas fa-sun' : 'fas fa-moon'"></i></button>
          <router-link to="/upload" class="top-btn"><i class="fas fa-plus"></i></router-link>
        </div>
      </div>
      <div class="content">
        <router-view />
      </div>
    </div>

    <CommandPalette :visible="showCommand" @close="showCommand = false" @execute="handleCommand" />
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAppStore } from '../stores/app'
import { useFilesStore } from '../stores/files'
import { useUploadStore } from '../stores/upload'
import { useWorkspaceStore } from '../stores/workspace'
import FolderTree from '../components/FolderTree.vue'
import CommandPalette from '../components/CommandPalette.vue'

const appStore = useAppStore()
const filesStore = useFilesStore()
const uploadStore = useUploadStore()
const wsStore = useWorkspaceStore()
const router = useRouter()
const route = useRoute()
const showCommand = ref(false)
const showNewWorkspace = ref(false)

watch(showNewWorkspace, (v) => {
  if (v) {
    const name = prompt('输入工作空间名称:')
    if (name) wsStore.createWorkspace(name)
    showNewWorkspace.value = false
  }
})

const stats = computed(() => appStore.stats)

const navItems = [
  { to: '/', label: '控制台', icon: 'fas fa-th-large' },
  { to: '/upload', label: '上传中心', icon: 'fas fa-cloud-upload-alt' },
  { to: '/library', label: '我的图床', icon: 'fas fa-folder' },
  { to: '/hall', label: '图床大厅', icon: 'fas fa-images' },
]

const currentTitle = computed(() => {
  const map = { '/': '控制台', '/upload': '上传中心', '/library': '我的图床', '/hall': '图床大厅', '/favorites': '收藏夹' }
  return map[route.path] || '控制台'
})

function formatSize(bytes) {
  if (!bytes) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  let i = 0
  while (bytes >= 1024 && i < units.length - 1) { bytes /= 1024; i++ }
  return bytes.toFixed(1) + ' ' + units[i]
}

function handleLogout() {
  appStore.logout()
  router.push('/login')
}

function selectFolder(id) {
  filesStore.currentFolderId = id
  filesStore.loadFiles()
}

async function showCreateFolder(parentId = null) {
  const name = prompt('输入文件夹名称:')
  if (name) {
    try {
      await filesStore.createFolder(name, parentId)
    } catch (e) {
      alert(e.response?.data?.error || '创建失败')
    }
  }
}

async function showRenameFolder(folder) {
  const name = prompt('输入新名称:', folder.name)
  if (name && name !== folder.name) {
    try {
      await filesStore.renameFolder(folder.id, name)
    } catch (e) {
      alert(e.response?.data?.error || '重命名失败')
    }
  }
}

async function handleDeleteFolder(folder) {
  if (confirm(`确定删除文件夹"${folder.name}"？子文件夹也会被删除。`)) {
    try {
      await filesStore.deleteFolder(folder.id)
    } catch (e) {
      alert(e.response?.data?.error || '删除失败')
    }
  }
}

function handleGlobalPaste(e) {
  const items = e.clipboardData?.items
  if (!items) return
  for (const item of items) {
    if (item.type.startsWith('image/')) {
      const file = item.getAsFile()
      if (file) {
        // Generate a meaningful filename from the paste
        const ext = file.type.split('/')[1] || 'png'
        const namedFile = new File([file], `paste-${Date.now()}.${ext}`, { type: file.type })
        uploadStore.addFiles([namedFile])
        uploadStore.startUpload()
      }
    }
  }
}

function handleKeydown(e) {
  if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
    e.preventDefault()
    showCommand.value = !showCommand.value
  }
}

function handleCommand(cmdId) {
  switch (cmdId) {
    case 'upload': router.push('/upload'); break
    case 'go-dashboard': router.push('/'); break
    case 'go-upload': router.push('/upload'); break
    case 'go-library': router.push('/library'); break
    case 'go-hall': router.push('/hall'); break
    case 'toggle-theme': appStore.toggleTheme(); break
    case 'view-grid': filesStore.setViewMode('gallery'); break
    case 'view-list': filesStore.setViewMode('list'); break
  }
}

onMounted(() => {
  appStore.loadStats()
  filesStore.loadFolders()
  document.addEventListener('paste', handleGlobalPaste)
  window.addEventListener('keydown', handleKeydown)
})

onUnmounted(() => {
  document.removeEventListener('paste', handleGlobalPaste)
  window.removeEventListener('keydown', handleKeydown)
})
</script>

<style scoped>
.app-layout { display: flex; min-height: 100vh; }
.sidebar { width: 290px; background: rgba(255,255,255,.72); backdrop-filter: blur(22px); border-right: 1px solid rgba(255,255,255,.7); padding: 20px; display: flex; flex-direction: column; overflow: auto; position: sticky; top: 0; height: 100vh; flex-shrink: 0; }
.logo { display: flex; align-items: center; gap: 16px; padding: 8px 10px; }
.logo-box { width: 54px; height: 54px; border-radius: 18px; background: linear-gradient(135deg, #2563eb, #7c3aed); display: flex; align-items: center; justify-content: center; color: white; box-shadow: 0 16px 40px rgba(37,99,235,.25); }
.logo h1 { font-size: 22px; font-weight: 800; }
.logo p { margin-top: 4px; font-size: 12px; color: #6b7280; }
.workspace { margin-top: 28px; padding: 18px; border-radius: 24px; background: linear-gradient(135deg, rgba(37,99,235,.08), rgba(124,58,237,.05)); }
.workspace-top { display: flex; justify-content: space-between; align-items: center; }
.workspace-label { font-size: 12px; color: #6b7280; }
.workspace-name { margin-top: 10px; font-size: 18px; font-weight: 800; }
.ws-add { width: 24px; height: 24px; border-radius: 6px; border: none; background: transparent; color: #9ca3af; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 12px; }
.ws-add:hover { background: rgba(255,255,255,.5); color: #2563eb; }
.workspace-list { margin-top: 12px; display: flex; flex-direction: column; gap: 4px; }
.ws-item { display: flex; align-items: center; justify-content: space-between; padding: 8px 12px; border-radius: 10px; cursor: pointer; font-size: 13px; font-weight: 600; color: #374151; transition: .15s; }
.ws-item:hover { background: rgba(255,255,255,.6); }
.ws-item.active { background: rgba(37,99,235,.1); color: #2563eb; }
.ws-delete { width: 20px; height: 20px; border-radius: 4px; border: none; background: transparent; color: #9ca3af; cursor: pointer; font-size: 10px; opacity: 0; transition: opacity .15s; }
.ws-item:hover .ws-delete { opacity: 1; }
.ws-delete:hover { color: #ef4444; }
.nav { margin-top: 28px; display: flex; flex-direction: column; gap: 8px; }
.nav-item { height: 52px; border-radius: 16px; padding: 0 16px; display: flex; align-items: center; gap: 14px; cursor: pointer; font-weight: 600; transition: .18s; color: #374151; text-decoration: none; }
.nav-item:hover { background: #f3f4f6; }
.nav-item.active { background: linear-gradient(135deg, #2563eb, #7c3aed); color: white; box-shadow: 0 16px 40px rgba(37,99,235,.22); }
.storage-card { margin-top: 28px; padding: 18px; border-radius: 20px; background: linear-gradient(135deg, #2563eb, #7c3aed); color: white; position: relative; overflow: hidden; }
.storage-card .label { font-size: 11px; opacity: .7; text-transform: uppercase; letter-spacing: 2px; }
.storage-card .value { font-size: 28px; font-weight: 800; margin-top: 8px; }
.storage-card .bar { margin-top: 14px; height: 8px; background: rgba(255,255,255,.2); border-radius: 99px; overflow: hidden; }
.storage-card .bar-fill { height: 100%; background: white; border-radius: 99px; }
.user-card { margin-top: auto; padding: 14px; border-radius: 18px; background: white; display: flex; align-items: center; gap: 12px; border: 1px solid #e5e7eb; }
.user-avatar { width: 42px; height: 42px; border-radius: 12px; background: linear-gradient(135deg, #2563eb, #7c3aed); display: flex; align-items: center; justify-content: center; color: white; font-weight: 800; font-size: 14px; }
.user-info { flex: 1; }
.user-info h4 { font-size: 14px; font-weight: 700; }
.user-info p { font-size: 12px; color: #6b7280; }
.user-logout { width: 36px; height: 36px; border-radius: 10px; border: none; background: #f3f4f6; cursor: pointer; display: flex; align-items: center; justify-content: center; color: #6b7280; }
.user-logout:hover { background: #fee2e2; color: #ef4444; }
.main-content { flex: 1; display: flex; flex-direction: column; min-width: 0; }
.topbar { height: 82px; padding: 0 28px; display: flex; align-items: center; justify-content: space-between; background: rgba(255,255,255,.7); backdrop-filter: blur(16px); border-bottom: 1px solid rgba(255,255,255,.7); position: sticky; top: 0; z-index: 50; }
.top-left h2 { font-size: 22px; font-weight: 800; }
.top-left p { margin-top: 4px; font-size: 13px; color: #6b7280; }
.top-right { display: flex; align-items: center; gap: 12px; }
.search-box { width: 340px; height: 46px; border-radius: 14px; background: white; border: 1px solid #e5e7eb; display: flex; align-items: center; gap: 10px; padding: 0 14px; }
.search-box input { width: 100%; border: none; outline: none; background: none; font-size: 14px; }
.top-btn { width: 44px; height: 44px; border-radius: 14px; background: white; display: flex; align-items: center; justify-content: center; cursor: pointer; border: 1px solid #e5e7eb; text-decoration: none; color: inherit; }
.top-btn:hover { transform: translateY(-2px); }
.content { flex: 1; overflow: auto; padding: 28px; }
</style>
