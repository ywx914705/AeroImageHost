<template>
  <Teleport to="body">
    <div v-if="visible" class="command-overlay" @click.self="$emit('close')">
      <div class="command-box">
        <div class="command-search">
          <i class="fas fa-search"></i>
          <input ref="searchInput" v-model="query" placeholder="搜索命令..." @keydown.esc="$emit('close')" @keydown.up.prevent="moveSelection(-1)" @keydown.down.prevent="moveSelection(1)" @keydown.enter="executeSelected" />
        </div>
        <div class="command-list">
          <div v-for="(group, gi) in filteredGroups" :key="gi">
            <div class="command-group">{{ group.label }}</div>
            <div v-for="(cmd, ci) in group.items" :key="cmd.id" class="command-item" :class="{ active: selectedIndex === getGlobalIndex(gi, ci) }" @click="executeCommand(cmd)" @mouseenter="selectedIndex = getGlobalIndex(gi, ci)">
              <i :class="cmd.icon"></i>
              <span>{{ cmd.label }}</span>
              <span v-if="cmd.shortcut" class="shortcut">{{ cmd.shortcut }}</span>
            </div>
          </div>
          <div v-if="filteredGroups.length === 0" class="command-empty">没有匹配的命令</div>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { ref, computed, watch, nextTick } from 'vue'

const props = defineProps({ visible: Boolean })
const emit = defineEmits(['close', 'execute'])

const query = ref('')
const selectedIndex = ref(0)
const searchInput = ref(null)

const allCommands = [
  { group: '文件操作', items: [
    { id: 'upload', label: '上传文件', icon: 'fas fa-cloud-upload-alt', shortcut: 'Ctrl+U' },
    { id: 'search', label: '搜索文件', icon: 'fas fa-search', shortcut: '/' },
    { id: 'new-folder', label: '新建文件夹', icon: 'fas fa-folder-plus' },
  ]},
  { group: '视图切换', items: [
    { id: 'view-grid', label: '网格视图', icon: 'fas fa-th', shortcut: '1' },
    { id: 'view-list', label: '列表视图', icon: 'fas fa-list', shortcut: '2' },
  ]},
  { group: '批量操作', items: [
    { id: 'select-all', label: '全选文件', icon: 'fas fa-check-double', shortcut: 'Ctrl+A' },
    { id: 'batch-delete', label: '批量删除', icon: 'fas fa-trash' },
    { id: 'batch-share', label: '批量分享', icon: 'fas fa-share-alt' },
  ]},
  { group: '导航', items: [
    { id: 'go-dashboard', label: '前往控制台', icon: 'fas fa-th-large' },
    { id: 'go-upload', label: '前往上传中心', icon: 'fas fa-cloud-upload-alt' },
    { id: 'go-library', label: '前往文件库', icon: 'fas fa-folder' },
    { id: 'go-hall', label: '前往大厅', icon: 'fas fa-images' },
  ]},
  { group: '设置', items: [
    { id: 'toggle-theme', label: '切换主题', icon: 'fas fa-moon', shortcut: 'Ctrl+Shift+D' },
  ]},
]

const filteredGroups = computed(() => {
  if (!query.value) return allCommands
  const q = query.value.toLowerCase()
  return allCommands.map(g => ({
    ...g,
    items: g.items.filter(i => i.label.toLowerCase().includes(q) || i.id.includes(q))
  })).filter(g => g.items.length > 0)
})

const totalItems = computed(() => filteredGroups.value.reduce((s, g) => s + g.items.length, 0))

function getGlobalIndex(gi, ci) {
  let idx = 0
  for (let i = 0; i < gi; i++) idx += filteredGroups.value[i].items.length
  return idx + ci
}

function moveSelection(dir) {
  selectedIndex.value = (selectedIndex.value + dir + totalItems.value) % totalItems.value
}

function executeSelected() {
  let idx = selectedIndex.value
  for (const g of filteredGroups.value) {
    if (idx < g.items.length) { executeCommand(g.items[idx]); return }
    idx -= g.items.length
  }
}

function executeCommand(cmd) {
  emit('execute', cmd.id)
  emit('close')
}

watch(() => props.visible, (v) => {
  if (v) { query.value = ''; selectedIndex.value = 0; nextTick(() => searchInput.value?.focus()) }
})
</script>

<style scoped>
.command-overlay { position: fixed; inset: 0; background: rgba(0,0,0,.45); display: flex; align-items: flex-start; justify-content: center; padding-top: 15vh; z-index: 9999; }
.command-box { width: 640px; background: white; border-radius: 24px; overflow: hidden; box-shadow: 0 30px 80px rgba(0,0,0,.22); animation: fadeUp .18s ease; }
@keyframes fadeUp { from { opacity: 0; transform: translateY(-8px); } to { opacity: 1; transform: none; } }
.command-search { display: flex; align-items: center; gap: 12px; padding: 0 24px; height: 64px; border-bottom: 1px solid #f1f5f9; }
.command-search i { color: #9ca3af; font-size: 16px; }
.command-search input { flex: 1; border: none; outline: none; font-size: 16px; background: none; }
.command-list { padding: 12px; max-height: 400px; overflow-y: auto; }
.command-group { font-size: 11px; font-weight: 700; color: #9ca3af; padding: 8px 12px 4px; text-transform: uppercase; letter-spacing: 0.5px; }
.command-item { display: flex; align-items: center; gap: 12px; padding: 10px 14px; border-radius: 10px; cursor: pointer; font-size: 14px; font-weight: 500; color: #374151; transition: background .1s; }
.command-item:hover, .command-item.active { background: #f3f4f6; }
.command-item i { width: 20px; font-size: 13px; color: #6b7280; text-align: center; }
.command-item .shortcut { margin-left: auto; font-size: 11px; color: #9ca3af; background: #f3f4f6; padding: 2px 8px; border-radius: 6px; font-family: monospace; }
.command-empty { text-align: center; padding: 32px; color: #9ca3af; font-size: 14px; }
</style>
