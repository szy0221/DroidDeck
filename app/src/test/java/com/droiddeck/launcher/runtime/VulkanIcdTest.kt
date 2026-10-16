package com.droiddeck.launcher.runtime

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * The ICD the guest is pointed at decides whether the session starts at all:
 * freedreno loads fine on a Mali GPU but enumerates no physical device, and
 * gamescope then dies before Steam or the desktop ever come up.
 */
class VulkanIcdTest {
    private fun manifests(vararg names: String): Array<File> {
        val dir = Files.createTempDirectory("droiddeck-icd").toFile()
        val files = names.map { File(dir, it).apply { writeText("{}") } }
        return files.sortedBy { it.name }.toTypedArray()
    }

    @Test fun adrenoStillGetsFreedreno() {
        assertEquals("freedreno_icd.aarch64.json",
            LinuxRuntime.pickIcd(manifests("freedreno_icd.aarch64.json", "lvp_icd.aarch64.json"), true)?.name)
    }

    @Test fun adrenoTakesWhateverItHasWhenFreedrenoIsAbsent() {
        assertEquals("lvp_icd.aarch64.json",
            LinuxRuntime.pickIcd(manifests("lvp_icd.aarch64.json", "virtio_icd.aarch64.json"), true)?.name)
    }

    @Test fun maliSkipsFreedrenoForTheSoftwareDriver() {
        assertEquals("lvp_icd.aarch64.json",
            LinuxRuntime.pickIcd(manifests("freedreno_icd.aarch64.json", "lvp_icd.aarch64.json"), false)?.name)
    }

    @Test fun maliPrefersSoftwareWhateverTheManifestIsCalled() {
        for (name in listOf("lvp_icd.aarch64.json", "lavapipe_icd.x86_64.json", "vk_swiftshader_icd.json")) {
            assertEquals(name, LinuxRuntime.pickIcd(manifests("freedreno_icd.aarch64.json", name), false)?.name)
        }
    }

    @Test fun maliTakesAnotherVendorDriverOverNothing() {
        assertEquals("panvk_icd.aarch64.json",
            LinuxRuntime.pickIcd(manifests("freedreno_icd.aarch64.json", "panvk_icd.aarch64.json"), false)?.name)
    }

    @Test fun maliForcesNothingWhenOnlyFreedrenoIsThere() {
        // Better to let the loader scan than to hand gamescope a driver that
        // cannot find a device - it fails either way, but the log stays honest.
        assertNull(LinuxRuntime.pickIcd(manifests("freedreno_icd.aarch64.json"), false))
    }

    @Test fun noManifestsAtAllMeansNoOverride() {
        assertNull(LinuxRuntime.pickIcd(emptyArray(), true))
        assertNull(LinuxRuntime.pickIcd(emptyArray(), false))
    }
}
