<template>
  <div>
    <div class="tree-node" :class="{ active: currentId === folder.id }" @click.stop="$emit('select', folder.id)" @contextmenu.prevent="showMenu">
      <button class="toggle" v-if="folder.children && folder.children.length" @click.stop="expanded = !expanded">
        <i :class="expanded ? 'fas fa-chevron-down' : 'fas fa-chevron-right'"></i>
      </button>
      <span v-else class="toggle"></span>
      <i class="fas fa-folder" :style="{ color: folder.color || '#f59e0b' }"></i>
      <span class="name">{{ folder.name }}</span>
      <span class="count" v-if="folder.file_count">{{ folder.file_count }}</span>
    </div>
    <div v-if="expanded && folder.children" class="children">
      <FolderTreeNode
        v-for="child in folder.children"
        :key="child.id"
        :folder="child"
        :current-id="currentId"
        @select="$emit('select', $event)"
        @create="$emit('create', $event)"
        @rename="$emit('rename', $event)"
        @delete="$emit('delete', $event)"
      />
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'

defineProps({
  folder: { type: Object, required: true },
  currentId: { type: Number, default: null }
})

const emit = defineEmits(['select', 'create', 'rename', 'delete'])
const expanded = ref(true)

function showMenu(e) {
  // 右键菜单将在后续实现
}
</script>

<style scoped>
.tree-node {
  display: flex; align-items: center; gap: 8px; padding: 8px 12px;
  border-radius: 10px; cursor: pointer; font-size: 13px; font-weight: 500;
  color: #374151; transition: all 0.15s; margin-left: 8px;
}
.tree-node:hover { background: #f5f7fb; }
.tree-node.active { background: rgba(37,99,235,.08); color: #2563eb; font-weight: 600; }
.toggle { width: 16px; height: 16px; border: none; background: transparent; color: #9ca3af; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 10px; flex-shrink: 0; }
.toggle:hover { color: #374151; }
.name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.count { font-size: 11px; color: #9ca3af; background: #f3f4f6; padding: 1px 6px; border-radius: 6px; }
.children { margin-left: 12px; }
</style>
