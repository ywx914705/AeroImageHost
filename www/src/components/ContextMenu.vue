<template>
  <div v-if="visible" class="context-menu" :style="{ left: x + 'px', top: y + 'px' }" @click.stop>
    <div class="context-item" v-for="item in items" :key="item.key" @click="$emit('select', item.key)">
      <i :class="item.icon"></i>
      <span>{{ item.label }}</span>
    </div>
  </div>
</template>

<script setup>
defineProps({
  visible: Boolean,
  x: Number,
  y: Number,
  items: { type: Array, default: () => [] }
})

defineEmits(['select'])
</script>

<style scoped>
.context-menu {
  position: fixed; width: 200px; background: white; border-radius: 14px;
  padding: 6px; box-shadow: 0 16px 48px rgba(0,0,0,.12); z-index: 9999;
  border: 1px solid #e5e7eb;
}
.context-item {
  display: flex; align-items: center; gap: 10px; padding: 10px 14px;
  border-radius: 8px; cursor: pointer; font-size: 13px; font-weight: 500;
  color: #374151; transition: background 0.1s;
}
.context-item:hover { background: #f3f4f6; }
.context-item i { width: 16px; font-size: 12px; color: #6b7280; }
</style>
